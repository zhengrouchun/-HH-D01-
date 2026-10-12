"""Verify and publish the completed role build; never flash automatically."""
from pathlib import Path
from datetime import datetime, timezone, timedelta
import hashlib
import json
import shutil
PROJECT = Path(__file__).resolve().parents[1]
SDK = PROJECT.parents[3]

def publish(role):
    folder = SDK/'output/clearchain'/('board_b' if role=='b' else 'board_a_'+role)
    manifest = json.loads((folder/'manifest.json').read_text(encoding='utf8'))
    assert manifest['role']==role
    for item in manifest['files']:
        path=folder/item['file']
        assert path.stat().st_size==item['size']
        assert hashlib.sha256(path.read_bytes()).hexdigest()==item['sha256']
    binary=(folder/'ws63-liteos-app-sign.bin').read_bytes()
    marker=b'[CLEAR UI] state-v4' if role=='b' else b'api-v2-armed-stop-20261006-v1'
    assert marker in binary
    if role=='b': assert b'calibrated inversion=OFF' in binary
    assert (b'OFFLINE BENCH:' in binary)==(role=='api_bench')
    source=folder/'ws63-liteos-app_all.fwpkg'
    sha=hashlib.sha256(source.read_bytes()).hexdigest()
    ready=SDK/'output/clearchain/_flash_ready'; ready.mkdir(parents=True,exist_ok=True)
    native_multi=role=='api_bench' and b'R200 BENCH MULTI' in binary
    label={'b':'BOARDB_V2_LCD_SLE','api':'BOARDA_API_V2',
           'api_bench':'BOARDA_OFFLINE_R200_MULTI' if native_multi else 'BOARDA_OFFLINE_REAL_RFID'}[role]
    date=datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d')
    target=ready/f'ws63-liteos-app_all_{label}_{date}_{sha[:8]}.fwpkg'
    shutil.copy2(source,target)
    assert hashlib.sha256(target.read_bytes()).hexdigest()==sha
    config=(folder/'mconfig.h').read_text()
    info={'role':role,'file':target.name,'size':target.stat().st_size,'sha256':sha,
          'display_protocol':4,'hardware_verified':False,'offline_bench':role=='api_bench',
          'r200_native_multi':native_multi,
          'http_enabled':'#define CONFIG_CLEARCHAIN_UPLOAD_ENABLED 1' in config and
                         '#define CONFIG_CLEARCHAIN_BACKEND_CONFIRMED 1' in config,
          'startup_marker':marker.decode(),'build_manifest':str(folder/'manifest.json')}
    target.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf8')
    print('Verified package:',target,flush=True)
    return target

if __name__=='__main__':
    import sys
    publish(sys.argv[1])
