"""只读已验证178输入，仅编译一个生产合同TU及可选Getter替换对象。

原9952源码/cache/proof全部只读；新输出独占。旧阶段链接原Getter对象，候选阶段
只替换唯一GL_Getter.cpp.o，其他所有直链输入逐SHA/大小验证并记录真实argv。
不configure、不构建依赖，不运行GPU；候选不能借原9952全库身份宣称新DSO资格。
"""
from pathlib import Path
from datetime import datetime,timezone
import argparse,hashlib,json,re,shlex,subprocess,sys
parser=argparse.ArgumentParser(description=__doc__)
for key in ('source','cache','proof','candidate','output'):parser.add_argument('--'+key,type=Path,required=True)
parser.add_argument('--expected-proof-sha',required=True)
parser.add_argument('--mode',choices=('old','candidate'),required=True)
args=parser.parse_args();source=args.source.resolve();cache=args.cache.resolve();candidate=args.candidate.resolve();out=args.output.resolve()
def require(value,message):
    if not value:raise ValueError(message)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
require(sys.platform=='linux','only selected Linux producer toolchain is supported')
require(not out.exists() and source not in out.parents and cache not in out.parents and candidate not in out.parents,'new external output required')
require(sha(args.proof)==args.expected_proof_sha,'independent original proof changed')
proof=json.loads(args.proof.read_text())
require(proof['sourceCommit']=='9952d8f6b9b438be36b0dba3828a749fbb7e3046' and proof['sourceClean'] is True,'selected old production source differs')
require(proof['containerPaths']=={'source':str(source),'build':str(cache)},'original source/cache namespace differs')
require(re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL='+re.escape(str(source))+r'$',(cache/'CMakeCache.txt').read_text(),re.M),'original CMake source differs')
for name,row in {**proof['files'],**proof['linkInputs']}.items():
    require(sha(cache/name)==row['sha256'] and (cache/name).stat().st_size==row['bytes'],'actual original input changed: '+name)
commands=subprocess.check_output(['ninja','-C',str(cache),'-t','commands','MobileGL'],text=True)
require(hashlib.sha256(commands.encode()).hexdigest()==proof['ninjaCommandsSha256'],'original Ninja commands changed')
last=shlex.split(commands.splitlines()[-1]);start=last.index('&&')+1;link=last[start:last.index('&&',start)]
actual=set()
for token in link:
    require(not token.startswith('@'),'response link requires a supported reader')
    if token.endswith(('.o','.a')):
        path=Path(token);path=(path if path.is_absolute() else cache/path).resolve()
        require(path.is_relative_to(cache),'original input escapes cache');actual.add(path.relative_to(cache).as_posix())
require(actual==set(proof['linkInputs']) and len(actual)==178,'complete actual 178 inputs differ')
compile_line=next(line for line in commands.splitlines() if 'Getter/GL_Getter.cpp.o' in line and ' -c ' in line)
tokens=shlex.split(compile_line);base=[];index=0
while index<len(tokens):
    token=tokens[index]
    if token in ('-o','-c','-MT','-MF'):index+=2;continue
    if token=='-MD':index+=1;continue
    base.append(token);index+=1
out.mkdir(parents=True);rows=[]
def run(name,argv):
    """同一前台进程记录实际编译/链接/执行；失败收据保留，不将崩溃当旧query反例。"""
    started=datetime.now(timezone.utc).isoformat()
    result=subprocess.run(argv,cwd=cache if name!='contract-run' else out,capture_output=True,text=True)
    (out/(name+'.log')).write_text(result.stdout+result.stderr)
    rows.append({'name':name,'command':argv,'startedAt':started,'finishedAt':datetime.now(timezone.utc).isoformat(),'exitCode':result.returncode})
    print(name+' exit='+str(result.returncode),flush=True)
    return result
getter=candidate/'MobileGL/MG_Impl/GLImpl/Getter/GL_Getter.cpp';test=candidate/'tools/cts/probe/copy_buffer_query_contract.cpp'
record={'startedAt':datetime.now(timezone.utc).isoformat(),'mode':args.mode,'originalProofSha256':args.expected_proof_sha,
        'originalSource':proof['sourceCommit'],'producerImage':proof['imageId'],'inputCount':len(actual),'getterSha256':sha(getter),'testSha256':sha(test),'commands':rows,
        'scope':'real production CPU object/Getter contract only; no new library/GPU/OHOS qualification'}
try:
    main=out/'copy-main.o';result=run('main-compile',base+['-c',str(test),'-o',str(main)])
    require(result.returncode==0,'contract TU compile failed')
    bootstrap=out/'copy-bootstrap.o'
    result=run('bootstrap-compile',base+['-DMGL_COPY_LIBRARY_BOOTSTRAP=1','-c',str(test),'-o',str(bootstrap)])
    require(result.returncode==0,'production initializer fixture compile failed')
    final=list(link)
    # 原178输入形成测试专用SO；唯一fixture出口只初始化生产模型，不提供替身GL实现。
    # 原GIT头仍是9952，候选仅替换Getter，混合来源在收据明确，不称正式新source产物。
    selected_library=out/'copy-production.so'
    final[final.index('-o')+1]=str(selected_library);final.append(str(bootstrap))
    if args.mode=='candidate':
        replacement=out/'getter-candidate.o';result=run('getter-compile',base+['-c',str(getter),'-o',str(replacement)])
        require(result.returncode==0,'production Getter compile failed')
        matches=[i for i,token in enumerate(final) if token.endswith('/Getter/GL_Getter.cpp.o')]
        require(len(matches)==1,'unique original Getter object missing');final[matches[0]]=str(replacement)
    result=run('production-so-link',final);require(result.returncode==0,'actual production SO link failed')
    result=run('contract-link',[base[0],str(main),'-ldl','-o',str(out/'copy-contract')])
    require(result.returncode==0,'public API contract link failed')
    result=run('contract-run',[str(out/'copy-contract'),str(selected_library)])
    functions=re.findall(r'^COPY_FUNCTION name=([A-Za-z0-9_]+) module=(.+)$',result.stdout,re.M)
    require(len(functions)==14 and len({name for name,_ in functions})==14 and
            all(Path(module).resolve()==selected_library.resolve() for _,module in functions),
            'actual public functions not uniquely owned by selected new SO')
    checks=re.findall(r'^COPY_CHECK ([a-z0-9-]+) pass=([01])$',result.stdout,re.M)
    failures=[name for name,value in checks if value=='0'];terminal=re.findall(r'^COPY_RESULT completed=1 checks=(\d+) failures=(\d+)$',result.stdout,re.M)
    require(len(terminal)==1 and int(terminal[0][0])==len(checks) and int(terminal[0][1])==len(failures),'actual contract incomplete')
    expected_queries={phase+'-'+side+'-'+kind for phase in ('default','distinct','switch','delete-unbind','after-copy','restored')
                      for side in ('read','write') for kind in ('int','int64','float','double','boolean')}
    expected_names=expected_queries|{'names','bind-two','pending-control','pending-read','pending-write',
                                      'copy-call','copy-read-16bytes','copy-unmap','cleanup-error'}
    require(len(checks)==70 and {name for name,_ in checks}==expected_names and
            sum(name=='pending-control' for name,_ in checks)==2,'fixed production contract denominator differs')
    if args.mode=='old':
        require(result.returncode==1 and set(failures)==expected_queries|{'pending-read','pending-write'} and len(failures)==62,
                'old production did not fail exactly the COPY queries and pending error preservation')
        require('copy-read-16bytes' not in failures and 'copy-unmap' not in failures,'old copy/map positive control failed')
    else:require(result.returncode==0 and not failures,'candidate production query contract failed')
    record.update(completed=True,contractChecks=len(checks),contractFailures=failures,expectedResultObserved=True,
                  actualLibrary={'path':str(selected_library),'sha256':sha(selected_library),'bytes':selected_library.stat().st_size},
                  actualFunctionOwnership=functions)
finally:
    # 再核完整178原字节，任何源对象变更都使本次收据失败，不覆盖旧proof。
    record['originalInputsPreserved']=all(sha(cache/name)==row['sha256'] and (cache/name).stat().st_size==row['bytes'] for name,row in proof['linkInputs'].items())
    record['finishedAt']=datetime.now(timezone.utc).isoformat()
    (out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
require(record['originalInputsPreserved'],'original cache inputs changed')
