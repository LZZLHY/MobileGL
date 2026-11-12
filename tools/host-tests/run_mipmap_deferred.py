"""编译真实CPU storage、原实现负控及ASan/UBSan；外置结果，成功后回收本轮可再生可执行文件。"""
from pathlib import Path
from datetime import datetime
import argparse,hashlib,json,subprocess,sys,time


def main():
    """依赖头显式来自固定只读源，不安装或初始化重依赖；此处不构建完整GL供应库。"""
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dependency-source',required=True);p.add_argument('--output',required=True);p.add_argument('--cxx',default='g++');p.add_argument('--keep',action='store_true')
    # WSL不能解释Windows Git工作树的绝对gitdir；允许调用者显式交付Windows Git提取的原件，
    # 不重写.git、不设置共享对象库override，也不把临时目录冒充独立Git仓。
    p.add_argument('--baseline-implementation');p.add_argument('--base-commit');a=p.parse_args()
    source=Path(__file__).resolve().parents[2];deps=Path(a.dependency_source).resolve();out=Path(a.output).resolve();assert not out.is_relative_to(source);out.mkdir(parents=True,exist_ok=False)
    cpp=source/'MobileGL/MG_State/GLState/TextureState/MipmapStorage.cpp';test=source/'tools/host-tests/mipmap_deferred_test.cpp';header=cpp.with_suffix('.h')
    sourceFiles=[cpp,header,cpp.parent/'TextureTypes.h',test];identities={str(f.relative_to(source)):hashlib.sha256(f.read_bytes()).hexdigest()for f in sourceFiles}
    old=out/'original-MipmapStorage.cpp'
    if a.baseline_implementation:
        assert a.base_commit and len(a.base_commit)==40
        base=a.base_commit;old.write_bytes(Path(a.baseline_implementation).read_bytes())
    else:
        base=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
        old.write_bytes(subprocess.check_output(['git','-C',str(source),'show',base+':MobileGL/MG_State/GLState/TextureState/MipmapStorage.cpp']))
    includes=[source/'MobileGL',cpp.parent,deps/'include',deps/'3rdparty',deps/'3rdparty/xxHash',deps/'3rdparty/Vulkan-Headers/include']
    common=[a.cxx,'-std=c++23','-O1','-g',*['-I'+str(x)for x in includes]];steps=[];passed=False
    try:
        for name,implementation,flags,expected in [('candidate',cpp,[],0),('original-control',old,[],1),('sanitized',cpp,['-fsanitize=address,undefined','-fno-omit-frame-pointer'],0)]:
            exe=out/name;command=common+flags+[str(test),str(implementation),'-o',str(exe)]
            start=time.time();r=subprocess.run(command,capture_output=True);(out/(name+'-compile.log')).write_bytes(r.stdout+r.stderr);steps.append({'phase':name+'-compile','command':command,'exitCode':r.returncode,'seconds':time.time()-start});assert r.returncode==0,r.stderr.decode('utf-8','replace')
            r=subprocess.run([str(exe)],capture_output=True,timeout=60);(out/(name+'.log')).write_bytes(r.stdout+r.stderr);steps.append({'phase':name,'exitCode':r.returncode,'expectedExitCode':expected});print(name,r.stdout.decode('utf-8','replace'),r.stderr.decode('utf-8','replace'),flush=True);assert r.returncode==expected
            if name=='original-control':assert b'FAIL reserve without initializing CPU texels'in r.stderr
        assert all(hashlib.sha256((source/name).read_bytes()).hexdigest()==digest for name,digest in identities.items()),'source changed during tests'
        passed=True
    finally:
        (out/'receipt.json').write_text(json.dumps({'at':datetime.now().astimezone().isoformat(),'passed':passed,'baseCommit':base,'candidateSourceSha256':identities,'originalImplementationSha256':hashlib.sha256(old.read_bytes()).hexdigest(),'steps':steps,'scope':'CPU storage only. Original implementation is compiled against current layout/options and ignores the added defer flag; not a whole historical GL binary. No target GPU/OHOS qualification.','executablesRetired':passed and not a.keep},indent=2)+'\n')
        if passed and not a.keep:
            for name in ['candidate','original-control','sanitized']:
                path=(out/name).resolve();assert path.parent==out;path.unlink()

if __name__=='__main__':main()
