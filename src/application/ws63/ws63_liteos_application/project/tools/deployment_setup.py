"""Configure the teammate-hosted API v2 and Board 1 Wi-Fi, then build its firmware."""
import argparse
import getpass
import ipaddress
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from datetime import datetime, timezone
from urllib.parse import urlsplit
from urllib.request import build_opener, ProxyHandler, Request
from urllib.error import URLError
from publish_role import publish, PROJECT, SDK
from laptop_network import device_host, laptop_url, current_ssid, lan_addresses

_run_status={}

def record_status(phase,status='running',url=None,error=None):
    """Persist progress and failures without recording SSID or Wi-Fi password."""
    _run_status.update(phase=phase,status=status,updated_at_utc=datetime.now(timezone.utc).isoformat())
    if url is not None: _run_status['backend_url']=url
    if error is not None: _run_status['error']=error
    path=SDK/'output/clearchain/deployment/setup_last_run.json'
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(_run_status,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
    return path

def validate_url(url):
    parts=urlsplit(url)
    if parts.scheme!='http' or parts.username or parts.password or parts.query or parts.fragment or parts.path not in ('','/'):
        raise ValueError('Use API base URL http://192.168.1.20:5000 (plain HTTP, no path).')
    ipaddress.IPv4Address(parts.hostname or '')
    port=parts.port or 80
    if not 1<=port<=65535: raise ValueError('Invalid port')
    return parts.hostname,port

def validate_state(state):
    if not isinstance(state,dict) or state.get('ok') is not True: raise ValueError('State did not return ok:true')
    if type(state.get('armed')) is not bool: raise ValueError('API v2 armed field missing')
    version=state.get('state_version')
    if type(version) is not int or not 0<=version<=0xffffffff: raise ValueError('Invalid state_version')
    for name,allowed in [('mode',['NONE','S1','S2','S3','S4','S5','CP']),
                         ('phase',['IDLE','READY','SCANNING','DONE','ERROR']),
                         ('screen_status',['NONE','APPROVED','MONITOR','REJECT']),
                         ('color',[None,'GREEN','ORANGE','RED']),('scan_direction',[None,'IN','OUT','MIXED'])]:
        if name not in state or state[name] not in allowed: raise ValueError('Invalid/missing '+name)
    for name,limit,nullable in [('tags_read',255,False),('tags_expected',65534,True),
                                ('progress_percent',100,True),('risk_percent',100,False)]:
        value=state.get(name)
        if name not in state or (value is None and not nullable) or (value is not None and
            (type(value) is not int or not 0<=value<=limit)): raise ValueError('Invalid/missing '+name)
    for name,limit in [('mode_label',23),('message',60)]:
        value=state.get(name)
        if not isinstance(value,str) or (name=='mode_label' and not value) or len(value)>limit or not value.isascii():
            raise ValueError('Invalid/missing '+name)
    batch=state.get('batch_id')
    if batch is not None and (not isinstance(batch,str) or len(batch.encode('utf8'))>64): raise ValueError('Invalid batch_id')
    return state

def get_json(url):
    # The board connects directly to the LAN server; preflight must do the same.
    with build_opener(ProxyHandler({})).open(Request(url,headers={'Accept':'application/json'}),timeout=4) as response:
        data=response.read(16385)
        if len(data)>16384: raise ValueError('API response too large')
        return json.loads(data)

def check_backend(url,allow_mock=False):
    health=get_json(url.rstrip('/')+'/health')
    version=str(health.get('api_version','')).lower().lstrip('v').split('.')[0]
    if health.get('ok') is not True or version!='2': raise ValueError('/health must report API v2')
    if health.get('fixture') and not allow_mock: raise ValueError('Mock detected: not a production result')
    state=validate_state(get_json(url.rstrip('/')+'/device/state'))
    print('API v2 preflight passed:',state['mode'],state['phase'],'armed=',state['armed'])
    return state

def write_profile(url,ssid,password,path):
    host,port=validate_url(url)
    device_host(host)
    if not 1<=len(ssid.encode('utf8'))<=32 or any(c in ssid for c in '\x00\r\n'): raise ValueError('SSID must be 1..32 UTF-8 bytes')
    if any(c in password for c in '\x00\r\n') or not (password=='' or 8<=len(password.encode('utf8'))<=63 or
                                  re.fullmatch(r'[0-9a-fA-F]{64}',password)):
        raise ValueError('Use empty open-network key, 8..63 byte WPA key, or 64 hex digits')
    values={'CLEARCHAIN_DISPLAY_SLE_SERVER':'y','CLEARCHAIN_DISPLAY_SLE_LINK_TEST':'n',
            'CLEARCHAIN_DEVICE_API':'y','CLEARCHAIN_DEVICE_BENCH':'n','SAMPLE_ENABLE':'n',
            'SPI_SUPPORT_MASTER':'n','CLEARCHAIN_UPLOAD_ENABLED':'y','CLEARCHAIN_BACKEND_CONFIRMED':'y',
            'CLEARCHAIN_DEVICE_PORT':str(port),'CLEARCHAIN_DEVICE_HOST':json.dumps(host),
            'CLEARCHAIN_WIFI_SSID':json.dumps(ssid,ensure_ascii=False),
            'CLEARCHAIN_WIFI_PASSWORD':json.dumps(password,ensure_ascii=False)}
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(''.join(k+'='+v+'\n' for k,v in values.items()),encoding='utf8')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url');parser.add_argument('--ssid');parser.add_argument('--check-only',action='store_true')
    parser.add_argument('--laptop',action='store_true',help='Use this laptop LAN address; do not start or install a backend')
    parser.add_argument('--port',type=int,default=5000,help='Backend listening port for laptop auto-configuration')
    args=parser.parse_args()
    _run_status.clear()
    record_status('backend_address_input')
    url=args.url or (laptop_url(args.port) if args.laptop else input('Teammate backend API URL (http://TEAMMATE_LAN_IP:5000): ').strip())
    host,port=validate_url(url);device_host(host)
    if args.laptop and host not in [entry['ip'] for entry in lan_addresses()]:
        raise ValueError('The backend address is not assigned to this laptop; check its current LAN IPv4')
    record_status('backend_preflight',url=url)
    check_backend(url)
    if args.check_only:
        record_status('backend_preflight','success');return
    suggested=current_ssid()
    record_status('wifi_ssid_input')
    ssid=args.ssid or input('Board 1 Wi-Fi SSID'+(' ['+suggested+']' if suggested else '')+': ').strip() or suggested
    if not ssid: raise ValueError('Enter the Wi-Fi SSID used by Board 1')
    record_status('wifi_password_input')
    print('Password input is hidden. Type the full Wi-Fi password and press Enter.')
    password=getpass.getpass('Wi-Fi password (hidden; Enter for open network): ')
    local=SDK/'output/clearchain/deployment/board_a_live.local.config'
    write_profile(url,ssid,password,local)
    env=os.environ.copy();env['CCACHE_DIR']=str(SDK/'output/clearchain/ccache')
    record_status('firmware_build')
    subprocess.run([sys.executable,str(PROJECT/'tools/build_dual.py'),'api','--profile',str(local)],check=True,cwd=SDK,env=env)
    package=publish('api')
    _run_status['package']=str(package)
    record_status('firmware_published','success')
    print('Flash this file to BOARD 1 only:',package)
    print('Backend URL:',url)
    print('Keep Board 2 on the matching v4 LCD/SLE package. Backend operation and registration remain your teammate\'s responsibility.')

def run_cli():
    try:main()
    except (ValueError,URLError,OSError,EOFError,subprocess.CalledProcessError,KeyboardInterrupt) as exc:
        detail='Input cancelled or console input ended' if isinstance(exc,(EOFError,KeyboardInterrupt)) else str(exc)
        try:
            path=record_status(_run_status.get('phase','startup'),'failed',error=detail)
            print('Diagnostic log:',path,file=sys.stderr)
        except OSError: pass
        print('Setup stopped:',detail,file=sys.stderr)
        print('Confirm the teammate backend URL is reachable. Wi-Fi password is hidden; finish input with Enter.',file=sys.stderr)
        return 1
    return 0

if __name__=='__main__':
    sys.exit(run_cli())
