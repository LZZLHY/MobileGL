"""以外部16B/offset8字节oracle核Float64表示限制；同时保护默认uniform软件routing。

输入含独立DSO SHA、native/reject预期；实际GL查询和绿/红像素分开保留。外部UBO/
SSBO即使没有double算术，也不能静默重排marker；默认uniform由软件按新IR布局写入。
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
parser.add_argument('--fp64',required=True,choices=('native','reject'));parser.add_argument('--report',type=Path,required=True)
args=parser.parse_args();require(not args.report.exists(),'refusing existing report')
require(hashlib.sha256(args.library.read_bytes()).hexdigest()==args.expected_library_sha,'independent library SHA mismatch')
scene=Scene(args);result={'startedAt':datetime.now(timezone.utc).isoformat(),'pid':os.getpid(),
    'librarySha256':args.expected_library_sha,'mode':args.mode,'backend':os.getenv('MOBILEGL_BACKEND_TYPE'),'cases':[]}
try:
    scene.setup()
    for kind,body in [('external-std140','#version 400 core\nlayout(std140) uniform Values{double unused;uint marker;};'),
                      ('external-std430','#version 430 core\nlayout(std430,binding=0) buffer Values{double unused;uint marker;};'),
                      ('managed-default','#version 400 core\nuniform double unused;uniform uint marker;')]:
        body+='out vec4 selected;void main(){selected=marker==7341u?vec4(0,1,0,1):vec4(1,0,0,1);}'
        program,_,compiled,linked=scene.program(body);row={'case':kind,'source':body,'compile':compiled,'link':linked}
        # 只有尚无等价布局路径的外UBO拒绝；这个uint-only std430场景已有真实flatten
        # 路由，旧库/native均绿，不能用预设LINK0把原合法支持误记成失败。
        expect_reject=args.fp64=='reject' and kind=='external-std140'
        valid=all(stage['status']==1 and stage['error']==0 for stage in compiled) and linked['error']==0
        if linked['status']==1:
            scene.use(program)
            if kind=='managed-default':
                location=scene.location(program,'marker');scene.call('glUniform1ui',None,[I,U],location,7341)
                actual=U();scene.call('glGetUniformuiv',None,[U,I,c.POINTER(U)],program,location,c.byref(actual))
                row.update(location=location,uniformActual=actual.value)
                valid=valid and location>=0 and actual.value==7341
            else:
                target=0x8a11 if kind=='external-std140' else 0x90d2
                binding=2 if kind=='external-std140' else 0
                if kind=='external-std140':
                    block=scene.call('glGetUniformBlockIndex',U,[U,C],program,b'Values')
                    scene.call('glUniformBlockBinding',None,[U,U,U],program,block,binding)
                    size=I();scene.call('glGetActiveUniformBlockiv',None,[U,U,U,c.POINTER(I)],program,block,0x8a40,c.byref(size))
                    names=(C*1)(b'marker');index=U();scene.call('glGetUniformIndices',None,[U,I,c.POINTER(C),c.POINTER(U)],program,1,names,c.byref(index))
                    offset=I();scene.call('glGetActiveUniformsiv',None,[U,I,c.POINTER(U),U,c.POINTER(I)],program,1,c.byref(index),0x8a3b,c.byref(offset))
                    row.update(blockIndex=block,declaredBlockBytes=size.value,declaredMarkerOffset=offset.value)
                    valid=valid and size.value==16 and offset.value==8
                buffer=U();scene.call('glGenBuffers',None,[I,c.POINTER(U)],1,c.byref(buffer));scene.buffers.append(buffer.value)
                data=(U*4)(0,0,7341,0);scene.call('glBindBuffer',None,[U,U],target,buffer.value)
                scene.call('glBufferData',None,[U,Z,P,U],target,16,data,0x88e8);scene.call('glBindBufferBase',None,[U,U,U],target,binding,buffer.value)
                row.update(bytes=list(data),oracleMarkerOffset=8,oracleBufferBytes=16)
            setup_error=scene.error();pixel=scene.pixel();row.update(setupError=setup_error,pixel=pixel)
            valid=valid and setup_error==0 and scene.color(pixel,[0,255,0,255])
        if expect_reject:
            valid=valid and linked['status']==0 and 'Float64' in linked['log'] and 'storage layout' in linked['log']
        else:valid=valid and linked['status']==1
        row['pass']=bool(valid);result['cases'].append(row)
    scene.retire();result.update(completed=True,retired=True,passed=all(row['pass'] for row in result['cases']),
                               version=scene.version,renderer=scene.renderer)
except Exception as error:result.update(completed=False,passed=False,error=type(error).__name__+': '+str(error))
result.update(finishedAt=datetime.now(timezone.utc).isoformat(),bindings=scene.bindings)
args.report.parent.mkdir(parents=True,exist_ok=True)
with args.report.open('x') as report:json.dump(result,report,indent=2)
print(json.dumps({key:value for key,value in result.items() if key not in ('bindings','cases')}))
raise SystemExit(0 if result['passed'] else 1)
