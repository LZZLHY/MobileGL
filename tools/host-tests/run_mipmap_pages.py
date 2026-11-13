"""用真实MipmapStorage验证零页、局部写、重定义和迁移；精确旧源码/头文件负控及ASan/UBSan独立运行。"""
from pathlib import Path
from datetime import datetime
import argparse,hashlib,json,subprocess

def main():
    """Linux执行；依赖只读借用，旧源由Windows Git导出，不修改工作树gitdir或共享依赖。"""
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dependency-source',required=True);p.add_argument('--baseline-root',required=True);p.add_argument('--output',required=True);p.add_argument('--base-commit',required=True);p.add_argument('--cxx',default='g++');a=p.parse_args()
    source=Path(__file__).resolve().parents[2];deps=Path(a.dependency_source).resolve();baseline=Path(a.baseline_root).resolve();out=Path(a.output).resolve();assert not out.is_relative_to(source);out.mkdir(parents=True,exist_ok=False)
    rel=Path('MobileGL/MG_State/GLState/TextureState');test=source/'tools/host-tests/mipmap_deferred_test.cpp';files=[source/rel/'MipmapByteStorage.h',source/rel/'MipmapStorage.h',source/rel/'MipmapStorage.cpp',test]
    identities={str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files};steps=[];passed=False
    try:
        for name,tree,flags,expected in [('candidate',source,[],0),('original-control',baseline,[],1),('sanitized',source,['-fsanitize=address,undefined','-fno-omit-frame-pointer'],0)]:
            includes=[tree/'MobileGL',tree/rel,source/'MobileGL',source/rel,deps/'include',deps/'3rdparty',deps/'3rdparty/xxHash',deps/'3rdparty/Vulkan-Headers/include']
            exe=out/name;cmd=[a.cxx,'-std=c++23','-O1','-g',*flags,*['-I'+str(x)for x in includes],str(test),str(tree/rel/'MipmapStorage.cpp'),'-o',str(exe)]
            r=subprocess.run(cmd,capture_output=True);(out/(name+'-compile.log')).write_bytes(r.stdout+r.stderr);steps.append({'name':name+'-compile','code':r.returncode,'command':cmd});assert not r.returncode,r.stderr.decode('utf-8','replace')
            r=subprocess.run([str(exe)],capture_output=True,timeout=60);(out/(name+'.log')).write_bytes(r.stdout+r.stderr);steps.append({'name':name,'code':r.returncode,'expected':expected});print(name,r.stdout.decode(),r.stderr.decode(),flush=True);assert r.returncode==expected
            if name=='original-control':assert b'first partial CPU access keeps untouched zero pages lazy'in r.stderr
        assert all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==sha for f,sha in identities.items())
        passed=True
    finally:
        (out/'receipt.json').write_text(json.dumps({'at':datetime.now().astimezone().isoformat(),'passed':passed,'baseCommit':a.base_commit,'sourceHashes':identities,'steps':steps,'scope':'real storage and exact old header/cpp negative control; no GPU or modpack qualification','executablesRetired':passed},indent=2)+'\n')
        if passed:
            for name in ['candidate','original-control','sanitized']:
                target=(out/name).resolve();assert target.parent==out;target.unlink()

if __name__=='__main__':main()
