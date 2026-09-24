import base64
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
with socket.socket() as s:
    s.bind(('127.0.0.1',0)); port = s.getsockname()[1]
with tempfile.TemporaryDirectory() as td:
    env = dict(os.environ,UJJAIN_DEVICE_TOKEN='a'*40,UJJAIN_ADMIN_PASSWORD='b'*24)
    proc = subprocess.Popen([sys.executable,str(ROOT/'collector/server.py'),'--db',str(Path(td)/'test.sqlite3'),'--port',str(port)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        url = f'http://127.0.0.1:{port}'
        for _ in range(40):
            try:
                urllib.request.urlopen(urllib.request.Request(url+'/api/history')).close(); break
            except urllib.error.HTTPError as exc:
                if exc.code == 401: break
            except OSError: time.sleep(0.05)
        payload = {k: 0 for k in ('s1_t','s1_rh','s2_t','s2_rh','temp_target','hum_target','rssi')}
        payload.update(device_id='ABCDEF123456',family='germination-chamber-ujjain-v1',fw='0.2.0-ujjain-bench',epoch=int(time.time()))
        for k in ('s1_ok','s2_ok','heater_on','heater_trip','heater_commissioned','hum_on','mist_on','exhaust_on'):
            payload[k] = False
        body = json.dumps(payload).encode()
        def send(path,auth,data=None):
            req=urllib.request.Request(url+path,data=data,headers={'Authorization':auth,'Content-Type':'application/json'})
            try:
                with urllib.request.urlopen(req) as res: return res.status,res.read()
            except urllib.error.HTTPError as ex: return ex.code,ex.read()
        assert send('/ingest','Bearer wrong',body)[0] == 403
        assert send('/ingest','Bearer '+'a'*40,body)[0] == 204
        admin='Basic '+base64.b64encode(('admin:'+'b'*24).encode()).decode()
        code,raw=send('/api/history',admin)
        assert code == 200 and json.loads(raw)[0]['payload']['device_id']=='ABCDEF123456'
        assert send('/api/history','Bearer '+'a'*40)[0] == 401
        assert send('/ingest','Bearer '+'a'*40,b'{"bad":1}')[0] == 400
    finally:
        proc.terminate(); proc.wait(timeout=3)
