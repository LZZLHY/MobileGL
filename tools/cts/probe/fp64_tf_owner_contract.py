"""实际TF捕获检验 frozen执行版本仍归原GL program，而非按新snapshot指针错认对象。

原生先校准 failed-link 旧exe可开始TF、当前原program不能relink、其它program可link。
输出保 actual shader/link/error 与捕获的三项float7，不以函数指针存在授予TF资格。
"""
import argparse
import ctypes as c
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
from fp64_link_contract import Scene,P,I,U,F,C,Z,require

parser=argparse.ArgumentParser();parser.add_argument('--mode',required=True,choices=('reference','mobilegl'))
parser.add_argument('--library',type=Path,required=True);parser.add_argument('--expected-library-sha',required=True)
parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
require(not args.report.exists(),'refusing existing report')
require(hashlib.sha256(args.library.read_bytes()).hexdigest()==args.expected_library_sha,'independent library SHA mismatch')
scene=Scene(args);result={'startedAt':datetime.now(timezone.utc).isoformat(),'pid':os.getpid(),
    'librarySha256':args.expected_library_sha,'mode':args.mode,'backend':os.getenv('MOBILEGL_BACKEND_TYPE')}
try:
    scene.setup();program=scene.call('glCreateProgram',U,[]);scene.programs.append(program)
    stages=[];shader_ids=[]
    for kind,body in [(0x8b31,'#version 400 core\nout float capture;void main(){vec2 p=vec2(gl_VertexID==1?3.0:-1.0,gl_VertexID==2?3.0:-1.0);gl_Position=vec4(p,0,1);capture=7.0;}'),
                      (0x8b30,'#version 400 core\nout vec4 selected;void main(){selected=vec4(0,1,0,1);}')]:
        shader=scene.call('glCreateShader',U,[U],kind);scene.shaders.append(shader);shader_ids.append(shader)
        stages.append(scene.compile(shader,body));scene.call('glAttachShader',None,[U,U],program,shader)
    names=(C*1)(b'capture');scene.call('glTransformFeedbackVaryings',None,[U,I,c.POINTER(C),U],program,1,names,0x8c8c)
    initial=scene.link(program);require(initial['status']==1 and initial['error']==0,'baseline TF program link failed')
    scene.use(program);buffer=U();scene.call('glGenBuffers',None,[I,c.POINTER(U)],1,c.byref(buffer));scene.buffers.append(buffer.value)
    scene.call('glBindBuffer',None,[U,U],0x8c8e,buffer.value);scene.call('glBufferData',None,[U,Z,P,U],0x8c8e,48,None,0x88e8)
    scene.call('glBindBufferBase',None,[U,U,U],0x8c8e,0,buffer.value);require(scene.error()==0,'baseline TF resource error')
    invalid=scene.compile(shader_ids[1],'#version 400 core\ninvalid_body !!!');failed=scene.link(program)
    scene.call('glBeginTransformFeedback',None,[U],4);begin_error=scene.error()
    if begin_error==0:
        same=scene.link(program)
        other,_,other_stages,other_link=scene.program('#version 400 core\nout vec4 selected;void main(){selected=vec4(1,0,0,1);}')
        scene.call('glDrawArrays',None,[U,I,I],4,0,3);draw_error=scene.error()
        scene.call('glEndTransformFeedback',None,[]);end_error=scene.error();scene.call('glFinish',None,[])
        values=(F*3)();scene.call('glBindBuffer',None,[U,U],0x8c8e,buffer.value)
        scene.call('glGetBufferSubData',None,[U,Z,Z,P],0x8c8e,0,12,values);read_error=scene.error()
        result.update(sameOwnerRelink=same,otherOwnerLink=other_link,values=list(values),drawError=draw_error,endError=end_error,readError=read_error)
        passed=(same['error']==1282 and same['status']==0 and other_link['status']==1 and other_link['error']==0
                and list(values)==[7,7,7] and draw_error==end_error==read_error==0)
    else:passed=False
    result.update(initialLink=initial,invalidCompile=invalid,failedLink=failed,beginError=begin_error)
    scene.retire();result.update(completed=True,retired=True,passed=passed and invalid['status']==0 and failed['status']==0,
                               version=scene.version,renderer=scene.renderer)
except Exception as error:result.update(completed=False,passed=False,error=type(error).__name__+': '+str(error))
result.update(finishedAt=datetime.now(timezone.utc).isoformat(),bindings=scene.bindings)
args.report.parent.mkdir(parents=True,exist_ok=True)
with args.report.open('x') as report:json.dump(result,report,indent=2)
print(json.dumps({key:value for key,value in result.items() if key!='bindings'}))
raise SystemExit(0 if result['passed'] else 1)
