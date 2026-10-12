"""API v2 protocol fixture. All verdicts are simulated, never production results."""
import argparse
from copy import deepcopy
import json
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
ACCESS_CODES={'S1':'PROD-7f2a','S2':'FDA-91xq','S3':'WARE-3kd8','S4':'PUB-c72m','S5':'PRIV-a9z1','CP':'VERIFY-q4m8'}
LABELS={'NONE':'Select a mode','S1':'S1 FACTORY','S2':'S2 FDA','S3':'S3 WAREHOUSE','S4':'S4 CHECKPOINT','S5':'S5 HOSPITAL','CP':'CP MOBILE INSPECTION'}
class Model:
    def __init__(self,batch='BATCH-TEST-001',expected=3):
        self.batch=batch;self.expected=expected;self.samples=[];self.unique=set();self.version=int(time.time())
        self.state={};self.reset()
    def reset(self):
        self.samples=[];self.unique=set();self.version+=1
        self.state={'ok':True,'state_version':self.version,'mode':'NONE','mode_label':LABELS['NONE'],
          'phase':'IDLE','armed':False,'source':'DEVICE','batch_id':None,'location':None,'tags_read':0,
          'tags_expected':None,'progress_percent':None,'screen_status':'NONE','color':None,'risk_percent':0,
          'scan_direction':None,'message':'MOCK ONLY - select a mode','buttons':{'history':True,'view_original':False,'view_image':False},
          'requested_view':'NONE','updated_at':'fixture'}
    def changed(self):
        self.version+=1;self.state['state_version']=self.version
        return 200,deepcopy(self.state)
    def error(self,status,code):return status,{'ok':False,'error':code,'message':'MOCK ONLY - '+code}
    def apply(self,path,p):
        if not isinstance(p,dict):return self.error(400,'BAD_REQUEST')
        s=self.state
        if path=='/device/command':
            cmd=p.get('command');mode=p.get('mode')
            if p.get('source') not in ('PHONE','DEVICE'):return self.error(400,'BAD_REQUEST')
            if cmd=='RESET':self.reset();return 200,deepcopy(self.state)
            if cmd=='SELECT_MODE':
                if mode not in ACCESS_CODES:return self.error(400,'BAD_REQUEST')
                if p.get('access_code')!=ACCESS_CODES[mode]:return self.error(403,'BAD_CODE')
                if s['phase']=='SCANNING':return self.error(409,'WRONG_PHASE')
                location=p.get('location')
                if mode=='CP' and location is not None:
                    if not isinstance(location,str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9 ._-]{0,39}',location):return self.error(400,'BAD_REQUEST')
                    location=location.replace(' ','-');location=location if location.startswith('CP-') else 'CP-'+location
                expected=p.get('tags_expected',self.expected if mode=='S1' else None)
                if expected is not None and (type(expected) is not int or expected<=0):return self.error(400,'BAD_REQUEST')
                batch=p.get('batch_id',None if mode=='CP' else self.batch)
                if batch is not None and (not isinstance(batch,str) or not batch):return self.error(400,'BAD_REQUEST')
                self.samples=[];self.unique=set()
                s.update(mode=mode,mode_label=LABELS[mode],phase='READY',armed=True,batch_id=batch,
                  location=location or ('CP-Mobile-Inspection' if mode=='CP' else 'MOCK-LOCATION'),
                  tags_expected=expected,tags_read=0,progress_percent=None,screen_status='NONE',color=None,risk_percent=0,
                  scan_direction=None,message='MOCK ONLY - '+mode+' selected',requested_view='NONE',source=p['source'])
                s['buttons']={'history':True,'view_original':mode=='CP','view_image':mode in ('S4','S5','CP')}
                return self.changed()
            if cmd=='STOP_SCAN':
                if s['phase']!='SCANNING':return self.error(409,'WRONG_PHASE')
                s.update(phase='READY',armed=False,message='MOCK ONLY - stopped',requested_view='NONE');return self.changed()
            if cmd=='START_SCAN':
                if s['mode']=='NONE' or s['phase'] not in ('READY','DONE','ERROR'):return self.error(409,'WRONG_PHASE')
                self.samples=[];self.unique=set()
                s.update(phase='SCANNING',armed=True,tags_read=0,progress_percent=None,screen_status='NONE',color=None,
                         risk_percent=0,scan_direction=None,message='MOCK ONLY - scanning',requested_view='NONE')
                return self.changed()
            if cmd=='OPEN_VIEW':
                key={'HISTORY':'history','ORIGINAL':'view_original','IMAGE':'view_image'}.get(p.get('view'))
                if key is None:return self.error(400,'BAD_REQUEST')
                if not s['buttons'][key]:return self.error(403,'VIEW_NOT_ALLOWED')
                s['requested_view']=p['view'];return self.changed()
            return self.error(400,'BAD_REQUEST')
        if path!='/device/scan':return self.error(404,'UNKNOWN_PATH')
        if p.get('mode')!=s['mode']:return self.error(409,'MODE_MISMATCH')
        if p.get('access_code')!=ACCESS_CODES.get(s['mode']):return self.error(403,'BAD_CODE')
        if not s['armed'] or s['phase'] not in ('READY','SCANNING'):return self.error(409,'WRONG_PHASE')
        if p.get('batch_id')!=s['batch_id']:return self.error(409,'MODE_MISMATCH')
        final=p.get('final');reads=p.get('readings')
        if type(final) is not bool or not isinstance(reads,list) or (not final and not reads):return self.error(400,'BAD_REQUEST')
        if not all(isinstance(x,dict) and isinstance(x.get('chip_uid'),str) and 1<=len(x['chip_uid'])<=64 and
                   type(x.get('rssi_dbm')) is int and -128<=x['rssi_dbm']<=127 for x in reads):return self.error(400,'BAD_REQUEST')
        self.samples.extend(deepcopy(reads));self.unique.update(x['chip_uid'] for x in reads)
        s['tags_read']=len(self.unique)
        expected=s['tags_expected'];s['progress_percent']=min(100,len(self.unique)*100//expected) if expected else None
        s['phase']='DONE' if final else 'SCANNING';s['requested_view']='NONE'
        # Only a protocol fixture: no actual registration, baseline, topology or IN/OUT algorithm.
        if final:
            s.update(screen_status='APPROVED' if reads or self.samples else 'REJECT',
                     color='GREEN' if reads or self.samples else 'RED',risk_percent=0 if self.samples else 100,
                     scan_direction='IN',message='MOCK ONLY - simulated verdict')
        else:s['message']='MOCK ONLY - scanning'
        return self.changed()
def make_server(host='127.0.0.1',port=0,batch='BATCH-TEST-001',expected=3):
    model=Model(batch,expected);lock=threading.Lock()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def reply(self,status,data):
            body=json.dumps(data).encode();self.send_response(status)
            self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)))
            self.end_headers();self.wfile.write(body)
        def do_GET(self):
            with lock:
                if self.path=='/health':self.reply(200,{'ok':True,'api_version':'2.0','fixture':True,'time':'mock'})
                elif self.path=='/device/state':self.reply(200,deepcopy(model.state))
                else:self.reply(404,{'ok':False,'error':'UNKNOWN_PATH'})
        def do_POST(self):
            try:
                length=int(self.headers.get('Content-Length','0'))
                if not 0<length<=16384:raise ValueError()
                payload=json.loads(self.rfile.read(length))
            except (ValueError,UnicodeDecodeError):self.reply(400,{'ok':False,'error':'BAD_REQUEST'});return
            with lock:
                status,obj=model.apply(self.path,payload);self.reply(status,obj)
    server=ThreadingHTTPServer((host,port),Handler);server.model=model;return server
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',default='127.0.0.1')
    p.add_argument('--port',type=int,default=5000);p.add_argument('--batch-id',default='BATCH-TEST-001')
    p.add_argument('--expected',type=int,default=3);a=p.parse_args()
    server=make_server(a.host,a.port,a.batch_id,a.expected)
    print(f'MOCK ONLY: http://{a.host}:{server.server_port}',flush=True);server.serve_forever()
