"""Laptop migration tests; no backend installation, flashing or OS changes."""
from pathlib import Path
import json
import os
import sys
import tempfile
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import deployment_setup as setup
from build_dual import profile_assignments
from laptop_network import device_host,usable_addresses,suggested_host,laptop_url

records=usable_addresses([
    dict(name='WLAN',description='Intel Wi-Fi',ip='10.231.28.178',gateway=True,up=True),
    dict(name='VMnet8',description='VMware Virtual Adapter',ip='192.168.169.1',gateway=False,up=True),
    dict(name='loopback',description='',ip='127.0.0.1',gateway=False,up=True),
    dict(name='old',description='Ethernet',ip='10.0.0.4',gateway=True,up=False),
])
assert len(records)==2 and suggested_host(records)=='10.231.28.178'
assert records[1]['virtual']
assert suggested_host(records+[dict(ip='192.168.1.3',virtual=False,gateway=True)]) is None
assert suggested_host([records[1]]) is None
for bad in ['127.0.0.1','0.0.0.0','169.254.1.3','224.0.0.1','255.255.255.255']:
    try:device_host(bad)
    except ValueError: pass
    else: raise AssertionError(bad)
with patch('laptop_network.lan_addresses',return_value=records),patch('builtins.input',return_value=''):
    assert laptop_url(5001)=='http://10.231.28.178:5001'
with patch('laptop_network.lan_addresses',return_value=records),patch('builtins.input',return_value='192.168.0.3'):
    try:laptop_url()
    except ValueError:pass
    else:raise AssertionError('Unassigned laptop IP accepted')

with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    cli=['deployment_setup.py','--laptop','--url','http://10.231.28.178:5001','--ssid','Board test wifi']
    password='example-"quoted"\\key'
    with patch.object(sys,'argv',cli),patch.object(setup,'SDK',root),\
         patch.object(setup,'lan_addresses',return_value=records),\
         patch.object(setup,'current_ssid',return_value='Detected Wi-Fi'),\
         patch.object(setup,'check_backend') as check,\
         patch.object(setup.getpass,'getpass',return_value=password),\
         patch.object(setup.subprocess,'run') as run,patch.object(setup,'publish',return_value=root/'new_board1.fwpkg') as publish:
        setup.main()
        check.assert_called_once_with('http://10.231.28.178:5001')
        local=root/'output/clearchain/deployment/board_a_live.local.config'
        values=profile_assignments(local)
        assert 'CLEARCHAIN_DEVICE_HOST=10.231.28.178' in values and 'CLEARCHAIN_DEVICE_PORT=5001' in values
        assert 'CLEARCHAIN_WIFI_PASSWORD='+password in values
        assert 'CLEARCHAIN_UPLOAD_ENABLED=y' in values and 'CLEARCHAIN_DEVICE_BENCH=n' in values
        command=run.call_args.args[0]
        assert command[-3:]==['api','--profile',str(local)]
        assert run.call_args.kwargs['env']['CCACHE_DIR']==str(root/'output/clearchain/ccache')
        publish.assert_called_once_with('api')
    for url in ['http://127.0.0.1:5000','http://192.168.1.99:5000']:
        with patch.object(sys,'argv',['deployment_setup.py','--laptop','--url',url]),\
             patch.object(setup,'SDK',root),\
             patch.object(setup,'lan_addresses',return_value=records),patch.object(setup,'check_backend') as check:
            try:setup.main()
            except ValueError:pass
            else:raise AssertionError('Invalid local deployment accepted')
            check.assert_not_called()
    with patch.object(sys,'argv',cli+['--check-only']),patch.object(setup,'SDK',root),\
         patch.object(setup,'lan_addresses',return_value=records),patch.object(setup,'check_backend') as check,\
         patch.object(setup.getpass,'getpass') as key,patch.object(setup.subprocess,'run') as run:
        setup.main();check.assert_called_once();key.assert_not_called();run.assert_not_called()
    with patch.object(sys,'argv',cli),patch.object(setup,'SDK',root),patch.object(setup,'lan_addresses',return_value=records),\
         patch.object(setup,'check_backend',side_effect=ValueError('API v1 or fixture rejected')),\
         patch.object(setup.getpass,'getpass') as key,patch.object(setup.subprocess,'run') as run:
        try:setup.main()
        except ValueError:pass
        else:raise AssertionError('Bad backend preflight bypassed')
        key.assert_not_called();run.assert_not_called()
    remote_url='http://192.168.44.22:5000'
    with patch.object(sys,'argv',['deployment_setup.py','--url',remote_url,'--ssid','Shared Wi-Fi']),\
         patch.object(setup,'SDK',root),patch.object(setup,'lan_addresses') as local_addresses,\
         patch.object(setup,'current_ssid',return_value=None),patch.object(setup,'check_backend') as check,\
         patch.object(setup.getpass,'getpass',return_value='zzzzzzzz'),\
         patch.object(setup.subprocess,'run') as run,patch.object(setup,'publish',return_value=root/'remote_board1.fwpkg'):
        assert setup.run_cli()==0
        local_addresses.assert_not_called();check.assert_called_once_with(remote_url)
        log=json.loads((root/'output/clearchain/deployment/setup_last_run.json').read_text())
        assert log['backend_url']==remote_url and log['status']=='success'
        assert 'zzzzzzzz' not in json.dumps(log) and 'Shared Wi-Fi' not in json.dumps(log)
    for cancelled in [EOFError(),KeyboardInterrupt()]:
        with patch.object(sys,'argv',['deployment_setup.py','--url',remote_url,'--ssid','Shared Wi-Fi']),\
             patch.object(setup,'SDK',root),patch.object(setup,'current_ssid',return_value=None),\
             patch.object(setup,'check_backend'),patch.object(setup.getpass,'getpass',side_effect=cancelled),\
             patch.object(setup.subprocess,'run') as run:
            assert setup.run_cli()==1;run.assert_not_called()
            log=json.loads((root/'output/clearchain/deployment/setup_last_run.json').read_text())
            assert log['phase']=='wifi_password_input' and log['status']=='failed'
    with patch.object(sys,'argv',['deployment_setup.py','--url',remote_url,'--ssid','Shared Wi-Fi']),\
         patch.object(setup,'SDK',root),patch.object(setup,'current_ssid',return_value=None),\
         patch.object(setup,'check_backend'),patch.object(setup.getpass,'getpass',return_value='z'),\
         patch.object(setup.subprocess,'run') as run:
        assert setup.run_cli()==1;run.assert_not_called()
        log=json.loads((root/'output/clearchain/deployment/setup_last_run.json').read_text())
        assert log['phase']=='wifi_password_input' and 'WPA' in log['error']
    with patch.object(setup,'build_opener') as opener:
        reply=opener.return_value.open.return_value.__enter__.return_value
        reply.read.return_value=b'{"ok":true}'
        assert setup.get_json('http://10.231.28.178:5001/health')['ok']
        assert opener.call_args.args[0].proxies=={}

print('PASS: local and teammate-hosted backend, address selection, non-default port, profile generation, check-only, direct LAN HTTP, z input validation, cancelled input and secret-free diagnostic log')
