"""按实际 Ninja 编译/链接命令复用候选对象，构建独立 CPU 合同与唯一旧epoch负控。

不改生产源码、CMake缓存或现有库；控制对象全部在本次D输出中。记录真实argv，
正例/反控各自stdout和退出。负控仅把生产key layout6改回已知旧5，不改测试断言。
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import argparse
import re
import sys
import json
import shlex
import subprocess

parser=argparse.ArgumentParser(description='用实际Ninja生产对象核CPU合同；输出必须仓外且新目录')
parser.add_argument('--build-dir',type=Path,required=True)
parser.add_argument('--output-dir',type=Path,required=True)
parser.add_argument('--source-dir',type=Path,default=Path(__file__).resolve().parents[3])
args=parser.parse_args();out=args.output_dir.resolve();cache=args.build_dir.resolve();source=args.source_dir.resolve()
if sys.platform!='linux':raise RuntimeError('此编译/对象资格入口仅支持已验证Linux Ninja工具链')
if out.exists() or out==source or source in out.parents:raise RuntimeError('输出必须是新仓外目录，不覆盖已有证据')
cache_text=(cache/'CMakeCache.txt').read_text()
home=re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$',cache_text,re.MULTILINE)
if not home or Path(home[1]).resolve()!=source:raise RuntimeError('CMake缓存不归当前显式source，拒借其它checkout缓存')
out.mkdir(parents=True)
test_source=source/'tools/cts/probe/fp64_ir_cache_contract.cpp'
commands=subprocess.check_output(['ninja','-C',str(cache),'-t','commands','MobileGL'],text=True).splitlines()
compile_command=next(line for line in commands if 'ProgramObject.cpp.o' in line and ' -c ' in line)
flags=shlex.split(compile_command);base=[];index=0
while index<len(flags):
    flag=flags[index]
    if flag in ('-o','-c','-MT','-MF'):index+=2;continue
    if flag=='-MD':index+=1;continue
    base.append(flag);index+=1
link=shlex.split(commands[-1]);start=link.index('&&')+1;end=link.index('&&',start)
link=link[start:end];link=[arg for arg in link if arg!='-shared' and not arg.startswith('-Wl,-soname,')]
records=[]


def run(name, args, expected=None):
    """逐条实际前台命令，失败仍保存全部收据；只有编译成功才执行后续控制。"""
    started=datetime.now(timezone.utc).isoformat()
    result=subprocess.run(args,cwd=cache,capture_output=True,text=True)
    (out/(name+'.log')).write_text(result.stdout+result.stderr)
    records.append({'name':name,'command':args,'start':started,'finish':datetime.now(timezone.utc).isoformat(),
                    'exitCode':result.returncode,'expectedExit':expected})
    print(name+' exit='+str(result.returncode),flush=True)
    if expected is None and result.returncode:raise RuntimeError('CPU build failed '+name)
    return result


try:
    main_object=out/'cpu-main.o'
    run('main-compile',base+['-c',str(test_source),'-o',str(main_object)])
    positive=list(link);positive[positive.index('-o')+1]=str(out/'cpu-contract');positive.append(str(main_object))
    run('positive-link',positive)
    passed=run('positive-run',[str(out/'cpu-contract'),str(out/'positive-raw')],expected=0)
    # 原 namespace 反例必须只改变 key epoch，不替换成功条件或生产 IR/link/cache 算法。
    original=(source/'MobileGL/MG_Util/ShaderTranspiler/TranslationCache.cpp').read_text()
    if original.count('kKeyLayoutVersion = 6u')!=1:raise RuntimeError('unexpected production epoch')
    legacy=out/'TranslationCache-epoch5.cpp';legacy.write_text(original.replace('kKeyLayoutVersion = 6u','kKeyLayoutVersion = 5u'))
    legacy_object=out/'epoch5.o'
    run('legacy-epoch-compile',base+['-I'+str(source/'MobileGL/MG_Util/ShaderTranspiler'),'-c',str(legacy),'-o',str(legacy_object)])
    control=list(positive);control[control.index('-o')+1]=str(out/'cpu-contract-legacy-epoch')
    matches=[i for i,arg in enumerate(control) if arg.endswith('/ShaderTranspiler/TranslationCache.cpp.o')]
    if len(matches)!=1:raise RuntimeError('expected unique cache object')
    control[matches[0]]=str(legacy_object)
    run('legacy-epoch-link',control)
    negative=run('legacy-epoch-run',[str(out/'cpu-contract-legacy-epoch'),str(out/'negative-raw'),'--cache-control-only'],expected=1)
    # TF身份阴性仅恢复旧pointer比较，生成独立控制DSO；不覆盖候选库或修改源码。
    program_source=(source/'MobileGL/MG_Impl/GLImpl/Program/GL_Program.cpp').read_text()
    owner_condition='MG_State::pGLContext->GetTransformFeedbackProgram()->GetExecutionOwnerLifetimeId() ==\n                programObject->GetExecutionOwnerLifetimeId()'
    if program_source.count(owner_condition)!=1:raise RuntimeError('expected unique TF owner condition')
    pointer_source=out/'GL_Program-pointer-control.cpp'
    pointer_source.write_text(program_source.replace(owner_condition,'MG_State::pGLContext->GetTransformFeedbackProgram().get() == programObject.get()'))
    pointer_object=out/'pointer-control.o'
    run('pointer-owner-compile',base+['-I'+str(source/'MobileGL/MG_Impl/GLImpl/Program'),'-c',str(pointer_source),'-o',str(pointer_object)])
    shared=shlex.split(commands[-1]);start=shared.index('&&')+1;shared=shared[start:shared.index('&&',start)]
    shared[shared.index('-o')+1]=str(out/'pointer-owner-control.so')
    program_objects=[i for i,arg in enumerate(shared) if arg.endswith('/Program/GL_Program.cpp.o')]
    if len(program_objects)!=1:raise RuntimeError('expected unique GL Program object')
    shared[program_objects[0]]=str(pointer_object)
    run('pointer-owner-link',shared)
    print(passed.stdout,flush=True);print(negative.stdout,flush=True)
    # 退出码不足以证明完整合同：逐必需check唯一、精确阴性、唯一末尾完成计数。
    def evidence_complete(result, negative_control):
        rows=re.findall(r'^CPU_CHECK ([a-z0-9-]+) pass=([01])$',result.stdout,re.MULTILINE)
        base_names={'aggregate-storage-float64-required','constant-array-struct-store-requires-float64',
                    'std430-layout-only-no-wide-values','actual-flatten-equivalent-layout-route',
                    'ubo-layout-no-equivalent-route',
                    'pointer-only-copy-requires-float64-storage','unused-function-no-float64-required',
                    'normal-cold-link','actual-l1-payload-input-calibration','real-l1-warm-hit',
                    'actual-legacy-entry-installed','legacy-epoch-cannot-bypass-float64-link',
                    'repeated-float64-rejection','current-snapshot-keeps-old-spv',
                    'last-current-retirement-releases-snapshot'}
        async_names={'current-pending-b-uniform-shadow-preserved','noncurrent-cancel-keeps-old-phase-contract',
                     'actual-noncurrent-pending-window-observed','actual-current-pending-window-observed'}
        names=base_names if negative_control else base_names|async_names
        failed={name for name,value in rows if value=='0'}
        expected_failed={'legacy-epoch-cannot-bypass-float64-link','repeated-float64-rejection'} if negative_control else set()
        terminal='CPU_RESULT completed=1 failures='+str(len(expected_failed))
        return (len(rows)==len(names) and {name for name,_ in rows}==names and failed==expected_failed
                and result.stdout.count('CPU_RESULT')==1 and result.stdout.rstrip().endswith(terminal))
    valid=(passed.returncode==0 and negative.returncode==1 and evidence_complete(passed,False)
           and evidence_complete(negative,True))
finally:
    (out/'cpu-receipt.json').write_text(json.dumps({'commands':records,
         'sourceMainSha256':hashlib.sha256(test_source.read_bytes()).hexdigest(),
         'scope':'actual production CPU objects/cache with poisoned legacy namespace; not oldGPU payload dump/device qualification'},indent=2))
raise SystemExit(0 if valid else 1)
