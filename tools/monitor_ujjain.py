"""Poll an existing chamber's LAN /api/status and serve private read-only history."""
import argparse
import base64
import hmac
import json
import os
import sqlite3
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs
from urllib.request import urlopen

PAGE = b'''<!doctype html><html lang="en"><meta charset="utf-8"><title>Ujjain chamber</title>
<style>body{font:16px system-ui;max-width:960px;margin:2em auto;padding:0 1em}table{border-collapse:collapse;width:100%}td,th{border:1px solid #ccc;padding:7px;text-align:left}.bad{color:#b00}</style>
<h1>Ujjain chamber</h1><p id="status">Loading...</p><table><thead><tr><th>Received</th><th>Sensor 1</th><th>Sensor 2</th><th>Outputs</th><th>Firmware</th></tr></thead><tbody id="rows"></tbody></table>
<script>async function run(){try{let r=await fetch('/api/history?limit=100');if(!r.ok)throw Error(r.status);let a=await r.json(),latest=a[0];document.getElementById('status').textContent=latest?(latest.error?'Last poll failed: '+latest.error:'Last received: '+new Date(latest.received_epoch*1000).toLocaleString()):'No polls yet';let rows=a.map(x=>{let p=x.payload||{},t=document.createElement('tr');let d=p.devices||[],ss=(v)=>v&&v.ok?(v.temp+' C / '+v.rh+'%'):'offline';for(let val of [new Date(x.received_epoch*1000).toLocaleString(),ss(p.s1),ss(p.s2),d.map(q=>q.kind+': '+(q.on?'ON':'off')).join(', '),p.fw||x.error||'']){let td=document.createElement('td');td.textContent=val;t.appendChild(td)}return t});document.getElementById('rows').replaceChildren(...rows)}catch(e){document.getElementById('status').textContent='Dashboard error: '+e}}run();setInterval(run,10000)</script></html>'''

def database(path):
    with sqlite3.connect(path) as db:
        db.execute('CREATE TABLE IF NOT EXISTS polls (id INTEGER PRIMARY KEY, received_epoch INTEGER NOT NULL, payload TEXT, error TEXT)')

def poll_once(device_url, db_path):
    payload, error = None, None
    try:
        with urlopen(device_url.rstrip('/')+'/api/status', timeout=4) as response:
            raw = response.read(16385)
        if len(raw) > 16384: raise ValueError('status too large')
        status = json.loads(raw)
        if status.get('family') != 'germination-chamber': raise ValueError('wrong device family')
        if not all(k in status for k in ('fw','s1','s2','devices')): raise ValueError('missing status fields')
        payload = json.dumps(status,separators=(',',':'))
    except Exception as exc:
        error = str(exc)[:180]
    with sqlite3.connect(db_path,timeout=5) as db:
        db.execute('INSERT INTO polls(received_epoch,payload,error) VALUES(?,?,?)',(int(time.time()),payload,error))
    return error

def serve(db_path, password, host, port):
    expected = 'Basic '+base64.b64encode(('admin:'+password).encode()).decode()
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if not hmac.compare_digest(self.headers.get('Authorization',''),expected):
                self.send_response(401); self.send_header('WWW-Authenticate','Basic realm="Ujjain monitor"'); self.end_headers(); return
            route=urlsplit(self.path)
            if route.path=='/': body,ctype=PAGE,'text/html; charset=utf-8'
            elif route.path=='/api/history':
                try: limit=min(max(int(parse_qs(route.query).get('limit',['100'])[0]),1),1000)
                except ValueError: self.send_error(400); return
                with sqlite3.connect(db_path,timeout=5) as db:
                    rows=db.execute('SELECT received_epoch,payload,error FROM polls ORDER BY id DESC LIMIT ?',(limit,)).fetchall()
                body=json.dumps([{'received_epoch':a,'payload':json.loads(b) if b else None,'error':c} for a,b,c in rows]).encode(); ctype='application/json'
            else: self.send_error(404); return
            self.send_response(200);self.send_header('Content-Type',ctype);self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    ThreadingHTTPServer((host,port),Handler).serve_forever()

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--device',required=True,help='Ujjain chamber LAN URL, such as http://192.168.1.45')
    p.add_argument('--db',default='ujjain-monitor.sqlite3')
    p.add_argument('--host',default='127.0.0.1');p.add_argument('--port',type=int,default=8765)
    p.add_argument('--interval',type=int,default=10)
    a=p.parse_args()
    password=os.environ.get('UJJAIN_MONITOR_PASSWORD','')
    if len(password)<16: p.error('Set UJJAIN_MONITOR_PASSWORD to a 16+ character password')
    database(a.db)
    def worker():
        while True:
            err=poll_once(a.device,a.db)
            if err: print('Poll failed:',err,flush=True)
            time.sleep(max(5,a.interval))
    threading.Thread(target=worker,daemon=True).start()
    print(f'Polling {a.device}; dashboard http://{a.host}:{a.port}',flush=True)
    serve(a.db,password,a.host,a.port)
