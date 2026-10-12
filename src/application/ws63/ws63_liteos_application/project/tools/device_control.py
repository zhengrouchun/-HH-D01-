"""Control the real API v2 from a PC; never synthesize RFID readings or verdicts."""
import argparse
import json
import re
import sys
from urllib.error import HTTPError, URLError
from urllib.request import Request, build_opener, ProxyHandler
from deployment_setup import validate_url, validate_state
from publish_role import PROJECT

def access_code(mode):
    text=(PROJECT/'clearchain_config.h').read_text(encoding='utf-8-sig')
    match=re.search(r'^#define\s+CLEARCHAIN_ACCESS_'+re.escape(mode)+r'\s+("[^"\r\n]*")',text,re.M)
    if not match: raise ValueError('Mode code not found in client configuration')
    return json.loads(match.group(1))

def exchange(url, payload=None):
    path='/device/state' if payload is None else '/device/command'
    request=Request(url.rstrip('/')+path,
                    data=None if payload is None else json.dumps(payload).encode(),
                    headers={'Accept':'application/json','Content-Type':'application/json'})
    try:
        with build_opener(ProxyHandler({})).open(request,timeout=4) as response:
            return validate_state(json.load(response))
    except HTTPError as exc:
        body=exc.read(2048).decode('utf8',errors='replace')
        raise ValueError('HTTP '+str(exc.code)+': '+body) from exc

def execute(url,cmd,mode=None,batch=None,expected=None,location=None):
    validate_url(url)
    if cmd=='STATE': return exchange(url)
    payload={'command':cmd,'source':'PHONE'}
    if cmd=='SELECT_MODE':
        if mode is None: raise ValueError('SELECT_MODE requires --mode')
        # Stop before changing a running mode, as required by API v2.
        if exchange(url)['phase']=='SCANNING':
            exchange(url,{'command':'STOP_SCAN','source':'PHONE'})
        payload.update(mode=mode,access_code=access_code(mode))
    if batch is not None: payload['batch_id']=batch
    if expected is not None: payload['tags_expected']=expected
    if location is not None: payload['location']=location
    return exchange(url,payload)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',required=True)
    parser.add_argument('--command',choices=['STATE','SELECT_MODE','START_SCAN','STOP_SCAN','RESET'],default='STATE')
    parser.add_argument('--mode',choices=['S1','S2','S3','S4','S5','CP'])
    parser.add_argument('--batch-id');parser.add_argument('--expected',type=int);parser.add_argument('--location')
    args=parser.parse_args()
    if args.expected is not None and not 1<=args.expected<=32:
        parser.error('This handheld supports a 1..32 tag batch')
    if (args.batch_id is not None or args.expected is not None) and args.command not in ('SELECT_MODE','START_SCAN'):
        parser.error('Batch and expected count apply only to SELECT_MODE/START_SCAN')
    if args.location is not None and not (args.command=='SELECT_MODE' and args.mode=='CP'):
        parser.error('Location applies only to SELECT_MODE CP')
    state=execute(args.url,args.command,args.mode,args.batch_id,args.expected,args.location)
    print(json.dumps(state,indent=2,ensure_ascii=False))

if __name__=='__main__':
    try: main()
    except (ValueError,URLError,OSError) as exc:
        print('Control failed:',exc,file=sys.stderr);sys.exit(1)
