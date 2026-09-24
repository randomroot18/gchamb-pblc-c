"""Small single-device HTTPS-reverse-proxy collector. Bind to loopback by default."""
import argparse
import base64
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hmac
import json
import os
import sqlite3
import time
from urllib.parse import urlsplit, parse_qs

SCHEMA = """CREATE TABLE IF NOT EXISTS samples (
  id INTEGER PRIMARY KEY, received_epoch INTEGER NOT NULL,
  device_id TEXT NOT NULL, device_epoch INTEGER NOT NULL,
  payload TEXT NOT NULL
); CREATE INDEX IF NOT EXISTS idx_samples_device_time
ON samples(device_id, received_epoch DESC);"""
FIELDS = ('device_id','family','fw','epoch','s1_ok','s1_t','s1_rh',
          's2_ok','s2_t','s2_rh','heater_on','heater_trip','heater_commissioned',
          'hum_on','mist_on','exhaust_on','temp_target','hum_target','rssi')
HTML = b'''<!doctype html><html><meta charset="utf-8"><title>Ujjain chamber</title>
<style>body{font:16px system-ui;max-width:850px;margin:3em auto;padding:0 1em}
pre{white-space:pre-wrap}table{border-collapse:collapse}td,th{padding:7px;border:1px solid #ddd}</style>
<h1>Ujjain chamber</h1><p id="state">Loading...</p><table><thead><tr><th>Received UTC</th><th>S1 C</th><th>S2 C</th><th>RH 1/2</th><th>Heater</th><th>Hum/Mist/Exh</th></tr></thead><tbody id="rows"></tbody></table>
<script>async function refresh(){try{let r=await fetch('/api/history?limit=100');if(!r.ok)throw Error(r.status);
let a=await r.json();document.getElementById('state').textContent=a.length?'Last seen: '+new Date(a[0].received_epoch*1000).toLocaleString():'No samples yet';
document.getElementById('rows').replaceChildren(...a.map(x=>{let d=x.payload,t=document.createElement('tr');
let values=[new Date(x.received_epoch*1000).toLocaleString(),d.s1_ok?d.s1_t:'offline',d.s2_ok?d.s2_t:'offline',
(d.s1_ok?d.s1_rh:'--')+' / '+(d.s2_ok?d.s2_rh:'--'),d.heater_on?'ON':'off',
['hum_on','mist_on','exhaust_on'].map(k=>d[k]?'ON':'off').join(' / ')];
for(let v of values){let c=document.createElement('td');c.textContent=v;t.appendChild(c)}return t}));
}catch(e){document.getElementById('state').textContent='Fetch failed: '+e}}
refresh();setInterval(refresh,30000)</script></html>'''


def serve(db_path, device_token, admin_password, host, port):
    with sqlite3.connect(db_path) as db:
        db.executescript(SCHEMA)
    expected_admin = 'Basic ' + base64.b64encode(('admin:'+admin_password).encode()).decode()
    expected_device = 'Bearer ' + device_token

    class Handler(BaseHTTPRequestHandler):
        def send_bytes(self, code, data, ctype='application/json'):
            self.send_response(code)
            self.send_header('Content-Type', ctype)
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(data)))
            if code == 401: self.send_header('WWW-Authenticate', 'Basic realm="Ujjain chamber"')
            self.end_headers()
            self.wfile.write(data)

        def authorized(self, expected):
            return hmac.compare_digest(self.headers.get('Authorization',''), expected)

        def do_POST(self):
            if self.path != '/ingest': return self.send_bytes(404,b'{}')
            if not self.authorized(expected_device): return self.send_bytes(403,b'{}')
            try:
                size = int(self.headers.get('Content-Length','0'))
                if not 1 <= size <= 2048: return self.send_bytes(413,b'{}')
                obj = json.loads(self.rfile.read(size))
                if not isinstance(obj,dict) or set(obj) != set(FIELDS): raise ValueError('fields')
                if not isinstance(obj['device_id'],str) or len(obj['device_id']) != 12: raise ValueError('id')
                if not isinstance(obj['epoch'],int) or abs(int(time.time())-obj['epoch']) > 300: raise ValueError('clock')
                if obj['family'] != 'germination-chamber-ujjain-v1': raise ValueError('family')
                if not all(type(obj[k]) is bool for k in ('s1_ok','s2_ok','heater_on','heater_trip',
                    'heater_commissioned','hum_on','mist_on','exhaust_on')): raise ValueError('bool')
            except (ValueError,TypeError,json.JSONDecodeError): return self.send_bytes(400,b'{}')
            with sqlite3.connect(db_path,timeout=10) as db:
                db.execute('INSERT INTO samples(received_epoch,device_id,device_epoch,payload) VALUES (?,?,?,?)',
                           (int(time.time()),obj['device_id'],obj['epoch'],json.dumps(obj,separators=(',',':'))))
            self.send_response(204); self.end_headers()

        def do_GET(self):
            if not self.authorized(expected_admin): return self.send_bytes(401,b'{}')
            route = urlsplit(self.path)
            if route.path == '/': return self.send_bytes(200,HTML,'text/html; charset=utf-8')
            if route.path != '/api/history': return self.send_bytes(404,b'{}')
            try: limit = min(max(int(parse_qs(route.query).get('limit',['100'])[0]),1),1000)
            except ValueError: return self.send_bytes(400,b'{}')
            with sqlite3.connect(db_path,timeout=10) as db:
                rows = db.execute('SELECT received_epoch,device_id,payload FROM samples ORDER BY id DESC LIMIT ?', (limit,)).fetchall()
            body = json.dumps([{'received_epoch':a,'device_id':b,'payload':json.loads(c)} for a,b,c in rows]).encode()
            self.send_bytes(200,body)

    server = ThreadingHTTPServer((host,port),Handler)
    print(f'Collector listening at http://{host}:{port}; HTTPS ingress required before remote use', flush=True)
    server.serve_forever()

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--db', default='ujjain.sqlite3'); ap.add_argument('--host',default='127.0.0.1')
    ap.add_argument('--port',type=int,default=8765)
    args = ap.parse_args()
    device_token = os.environ.get('UJJAIN_DEVICE_TOKEN','')
    admin_password = os.environ.get('UJJAIN_ADMIN_PASSWORD','')
    if len(device_token) < 32 or len(admin_password) < 16:
        ap.error('Set UJJAIN_DEVICE_TOKEN (32+ chars) and UJJAIN_ADMIN_PASSWORD (16+ chars) in environment')
    serve(args.db,device_token,admin_password,args.host,args.port)
