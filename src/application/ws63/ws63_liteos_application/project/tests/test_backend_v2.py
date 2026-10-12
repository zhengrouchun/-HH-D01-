"""Loopback HTTP contract and deployment preflight checks (no hardware)."""
from pathlib import Path
import json
import sys
import tempfile
import threading
from urllib.request import Request,urlopen
from urllib.error import HTTPError
from mock_device_backend import make_server
TOOLS=Path(__file__).resolve().parents[1]/'tools'
sys.path.insert(0,str(TOOLS))
from deployment_setup import check_backend,write_profile,validate_url,validate_state
from build_dual import profile_assignments
from device_control import execute

server=make_server();thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
url=f'http://127.0.0.1:{server.server_port}'
def request(path,payload=None):
    r=Request(url+path,data=None if payload is None else json.dumps(payload).encode(),headers={'Content-Type':'application/json'})
    try:
        with urlopen(r,timeout=2) as reply:return reply.status,json.load(reply)
    except HTTPError as reply:return reply.code,json.load(reply)
def command(cmd,**fields):return request('/device/command',dict(command=cmd,source='PHONE',**fields))
def scan(readings,final=False,mode='S1'):
    p=dict(mode=mode,access_code={'S1':'PROD-7f2a','CP':'VERIFY-q4m8'}[mode],final=final,readings=readings)
    if mode!='CP':p['batch_id']='BATCH-TEST-001'
    return request('/device/scan',p)
try:
    assert check_backend(url,allow_mock=True)['armed'] is False
    try:check_backend(url)
    except ValueError as exc:assert 'Mock' in str(exc)
    else:raise AssertionError('Mock was accepted as production')
    assert command('SELECT_MODE',mode='S1',access_code='bad')[0]==403
    code,s=command('SELECT_MODE',mode='S1',access_code='PROD-7f2a');assert code==200 and s['armed']
    sample={'chip_uid':'000000000000000000078908','rssi_dbm':-52}
    code,s=scan([sample,dict(sample,rssi_dbm=-58)]);assert code==200 and s['tags_read']==1
    assert len(server.model.samples)==2
    assert command('SELECT_MODE',mode='S4',access_code='PUB-c72m')[0]==409
    code,s=command('STOP_SCAN');assert code==200 and s['armed'] is False
    assert scan([sample])[0]==409 and scan([],True)[0]==409
    assert command('START_SCAN')[0]==200
    assert scan([],False)[0]==400
    code,s=scan([],True);assert code==200 and s['phase']=='DONE'
    assert scan([sample])[0]==409
    assert command('OPEN_VIEW',view='ORIGINAL')[0]==403
    code,s=command('SELECT_MODE',mode='CP',access_code='VERIFY-q4m8');assert code==200 and s['batch_id'] is None
    assert scan([sample],True,'CP')[0]==200
    assert command('OPEN_VIEW',view='ORIGINAL')[0]==200
    assert command('RESET')[1]['mode']=='NONE'
    assert execute(url,'SELECT_MODE','S1','BATCH-TEST-001',3)['mode']=='S1'
    assert scan([sample])[0]==200
    assert execute(url,'SELECT_MODE','S4')['mode']=='S4'
    assert execute(url,'START_SCAN')['phase']=='SCANNING'
    assert execute(url,'STOP_SCAN')['armed'] is False
    assert execute(url,'RESET')['mode']=='NONE'
    validate_state(request('/device/state')[1])
    for bad in ['https://127.0.0.1:5000','http://u:p@127.0.0.1:5000','http://127.0.0.1:5000/device/state']:
        try:validate_url(bad)
        except ValueError:pass
        else:raise AssertionError(bad)
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)/'local.config'
        password='fixture-"quoted"\\key'
        write_profile('http://192.168.20.5:5000','Test SSID',password,p)
        values=profile_assignments(p)
        assert 'CLEARCHAIN_WIFI_PASSWORD='+password in values
        assert 'CLEARCHAIN_UPLOAD_ENABLED=y' in values and 'CLEARCHAIN_DEVICE_BENCH=n' in values
    print('PASS: loopback API v2 GET/commands/scans, Stop/DONE rejection, repeated RSSI, CP, permissions, preflight and quoted credential round-trip')
finally:
    server.shutdown();server.server_close();thread.join(timeout=2)
