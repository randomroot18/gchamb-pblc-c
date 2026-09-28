#!/usr/bin/env python3
"""Build a manifest from the exact application binary being published."""
import argparse,hashlib,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('binary',type=Path);p.add_argument('--url',required=True);p.add_argument('--version',default='1.0.1-ota-test');p.add_argument('--output',type=Path,default=Path('manifest.json'));a=p.parse_args()
if not a.url.startswith('https://') or any(x in a.url for x in '\r\n@'):p.error('Use a direct HTTPS URL')
if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?',a.version):p.error('Invalid version')
b=a.binary.read_bytes();marker=f'SHUNYA_IMAGE:shunya-ujjain-chamber:{a.version}:END'.encode()
if not b or len(b)>0x1c0000 or b[0]!=0xe9:p.error('Not an ESP application image fitting the supplied OTA slot')
if marker not in b:p.error('Embedded Ujjain family/version does not match')
data={'version':a.version,'url':a.url,'sha256':hashlib.sha256(b).hexdigest(),'device_family':'shunya-ujjain-chamber'}
a.output.write_text(json.dumps(data,indent=2)+'\n');print(f'{a.output}: {len(b)} bytes; SHA-256 {data["sha256"]}')
