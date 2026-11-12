"""编译真实裁剪helper及ASan/UBSan版本；成功后只删除本轮亲建可执行文件。"""
from pathlib import Path
from datetime import datetime
import argparse,subprocess,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[2];source=root/'tools/host-tests/texture_upload_region_test.cpp';header=root/'MobileGL/MG_Backend/DirectVulkan/Renderer/TextureUploadRegion.h'
def linux(p):q=p.as_posix();return '/mnt/'+q[0].lower()+q[2:]
r={'at':datetime.now().astimezone().isoformat(),'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'headerSha256':hashlib.sha256(header.read_bytes()).hexdigest(),'passed':False,'steps':[]}
try:
 for name,flags in [('normal',[]),('sanitized',['-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
  exe=out/name;built=subprocess.run(['wsl.exe','-d','Ubuntu','--','g++','-std=c++20','-O1','-g',*flags,linux(source),'-o',linux(exe)],capture_output=True);(out/(name+'-build.log')).write_bytes(built.stdout+built.stderr);assert built.returncode==0
  run=subprocess.run(['wsl.exe','-d','Ubuntu','--',linux(exe)],capture_output=True,timeout=30);(out/(name+'.log')).write_bytes(run.stdout+run.stderr);r['steps'].append({'name':name,'exitCode':run.returncode,'output':run.stdout.decode()});assert run.returncode==0;exe.unlink()
 r['passed']=True
finally:(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
print(json.dumps(r))
