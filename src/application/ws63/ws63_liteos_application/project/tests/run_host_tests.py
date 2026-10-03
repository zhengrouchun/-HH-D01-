from pathlib import Path
import os
import subprocess

TESTS=Path(__file__).resolve().parent
PROJECT=TESTS.parent
SDK=PROJECT.parents[3]
WS=SDK.parents[1]
GCC=WS/'mingw64/bin/gcc.exe'
OUT=SDK/'output/clearchain/host_tests'
OUT.mkdir(parents=True,exist_ok=True)
env=os.environ.copy(); env['PATH']=str(GCC.parent)+os.pathsep+env['PATH']
exe=OUT/'wire_batch_tests.exe'
cmd=[str(GCC),'-std=c99','-Wall','-Wextra','-Werror','-I'+str(TESTS/'stubs'),'-I'+str(PROJECT),
     str(TESTS/'test_wire_and_batch.c'),str(PROJECT/'clearchain_display_protocol.c'),
     str(PROJECT/'r200_protocol.c'),str(PROJECT/'r200_reader.c'),'-o',str(exe)]
subprocess.run(cmd,check=True,env=env)
result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,env=env)
print(result.stdout,end=''); (OUT/'results.txt').write_text(result.stdout,encoding='utf8')
exe=OUT/'upload_gate_tests.exe'
cmd=[str(GCC),'-std=c99','-Wall','-Wextra','-Werror','-I'+str(TESTS/'stubs'),'-I'+str(PROJECT),
     str(TESTS/'test_upload_gate.c'),str(PROJECT/'clearchain_http.c'),'-o',str(exe)]
subprocess.run(cmd,check=True,env=env)
result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,env=env)
print(result.stdout.splitlines()[-1])
with (OUT/'results.txt').open('a',encoding='utf8') as log:
    log.write(result.stdout.splitlines()[-1]+'\n')
exe=OUT/'lcd_trace_tests.exe'
cmd=[str(GCC),'-std=c99','-Wall','-Wextra','-Werror','-I'+str(TESTS/'stubs'),'-I'+str(PROJECT),
     str(TESTS/'test_lcd_trace.c'),str(PROJECT/'clearchain_lcd.c'),'-o',str(exe)]
subprocess.run(cmd,check=True,env=env)
result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,env=env)
print(result.stdout,end='')
with (OUT/'results.txt').open('a',encoding='utf8') as log:
    log.write(result.stdout)
exe=OUT/'device_contract_tests.exe'
cmd=[str(GCC),'-std=c99','-Wall','-Wextra','-Werror','-I'+str(TESTS/'stubs'),
     '-I'+str(PROJECT),'-I'+str(SDK/'open_source/cjson/cjson'),
     str(TESTS/'test_device_contract.c'),str(PROJECT/'clearchain_device_state.c'),
     str(PROJECT/'clearchain_device_http.c'),str(SDK/'open_source/cjson/cjson/cJSON.c'),
     '-o',str(exe)]
subprocess.run(cmd,check=True,env=env)
result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,env=env)
print(result.stdout,end='')
with (OUT/'results.txt').open('a',encoding='utf8') as log:
    log.write(result.stdout)
