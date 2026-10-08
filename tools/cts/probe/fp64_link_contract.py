"""冷进程核 Float64 限制与 current/last-link 分离，直接使用选定生产 GL/EGL。

输入：显式 DSO、独立实取 SHA、reference/MobileGL 模式和 native/reject Float64
预期。输出：拒绝覆盖的完整 JSON，包含每个绑定函数 owner、输入、状态、错误与
像素。退出0只表示本固定九项合同通过；reject 后端仍不具备宣告 Core4 所需精度。
没有 fallback、函数表覆盖、软件生成 shader 结果或由当前输出自制 golden。
"""
import argparse
import ctypes as c
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path

P=c.c_void_p; I=c.c_int; U=c.c_uint; F=c.c_float; C=c.c_char_p; Z=c.c_ssize_t
CASES=('resource-baseline','failed-current-render','failed-query-setter-boundary',
       'old-executable-live-resource','repeated-failed-current','successful-relink-and-retirement',
       'unused-float64-eliminated','dynamic-float64-cold-hot','pack-float64-pending-error')


def require(value, message):
    """显式执行并检查真实API结果；Python -O也不能去掉生命周期副作用或判据。"""
    if not value:raise RuntimeError(message)


class DlInfo(c.Structure):
    """Linux dladdr ABI；仅读取 owner，不参与 GL/EGL 派发或改写返回值。"""
    _fields_=[('file',C),('base',P),('symbol',C),('address',P)]


class Scene:
    """一个冷 context 与其拥有的 GL 资源；所有 API 均经实际指定 provider 绑定。"""
    def __init__(self, args):
        self.args=args;self.path=args.library.resolve();self.bindings=[];self.rows=[]
        self.programs=[];self.shaders=[];self.textures=[];self.buffers=[]
        self.display=self.surface=self.context=None;self.fbo=self.vao=0
        self.functions={};self.lib=c.CDLL(str(self.path));self.runtime=c.CDLL(None)
        self.runtime.dladdr.argtypes=[P,c.POINTER(DlInfo)];self.runtime.dladdr.restype=I
        self.getproc=self.egl('eglGetProcAddress',P,[C])

    def observe(self, name, address):
        """MobileGL 的实际调用必须同 requested 映像；参考 GL 可在系统 dispatch 映像。"""
        info=DlInfo();resolved=self.runtime.dladdr(P(address),c.byref(info))
        owner=Path(os.fsdecode(info.file)).resolve() if resolved and info.file else None
        self.bindings.append({'name':name,'address':address,'owner':str(owner) if owner else None,
                              'imageBase':info.base,'requestedImage':owner==self.path})
        if not address or not owner or (self.args.mode=='mobilegl' and owner!=self.path):
            raise RuntimeError('missing/foreign production function '+name)

    def egl(self, name, result, types):
        """EGL 入口实取并校验，句柄依赖不能借同名函数充作当前库。"""
        fun=getattr(self.lib,name);self.observe(name,c.cast(fun,P).value)
        fun.restype=result;fun.argtypes=types;return fun

    def gl(self, name, result, types):
        """所有 GL 入口走 provider 的真实 getProc；缺失直接拒绝，不尝试另一个 provider。"""
        if name not in self.functions:
            address=self.getproc(name.encode());self.observe(name,address)
            self.functions[name]=c.CFUNCTYPE(result,*types)(address)
        return self.functions[name]

    def call(self, name, result, types, *values):
        """调用已验证函数；错误由场景在明确时点取出，避免吞 pending error。"""
        return self.gl(name,result,types)(*values)

    def error(self):return self.call('glGetError',U,[])

    def setup(self):
        """真实 desktop4.0 EGL context、8x8 RGBA8 FBO，与 sampling/UBO 资源分离。"""
        getdisplay=self.egl('eglGetDisplay',P,[P]);init=self.egl('eglInitialize',U,[P,c.POINTER(I),c.POINTER(I)])
        bind=self.egl('eglBindAPI',U,[U]);choose=self.egl('eglChooseConfig',U,[P,c.POINTER(I),c.POINTER(P),I,c.POINTER(I)])
        self.display=getdisplay(None);require(init(self.display,None,None)==1,'eglInitialize failed');require(bind(0x30a2)==1,'eglBindAPI desktop failed')
        config=P();n=I();attrs=(I*13)(0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038)
        require(choose(self.display,attrs,c.byref(config),1,c.byref(n))==1 and n.value>0,'eglChooseConfig failed')
        self.surface=self.egl('eglCreatePbufferSurface',P,[P,P,c.POINTER(I)])(self.display,config,(I*5)(0x3057,8,0x3056,8,0x3038))
        self.context=self.egl('eglCreateContext',P,[P,P,P,c.POINTER(I)])(self.display,config,None,(I*7)(0x3098,4,0x30fb,0,0x30fd,1,0x3038))
        self.makecurrent=self.egl('eglMakeCurrent',U,[P,P,P,P])
        require(self.surface and self.context,'missing actual EGL surface/context')
        require(self.makecurrent(self.display,self.surface,self.surface,self.context)==1,'eglMakeCurrent failed')
        getstring=self.gl('glGetString',C,[U]);self.version=getstring(0x1f02).decode();self.renderer=getstring(0x1f01).decode()
        major=I();minor=I();self.call('glGetIntegerv',None,[U,c.POINTER(I)],0x821b,c.byref(major));self.call('glGetIntegerv',None,[U,c.POINTER(I)],0x821c,c.byref(minor))
        require((major.value,minor.value)>=(4,0) and self.error()==0,'invalid actual API/version query')
        name=U();self.call('glGenVertexArrays',None,[I,c.POINTER(U)],1,c.byref(name));self.vao=name.value
        self.call('glBindVertexArray',None,[U],self.vao)
        texture=self.texture(8,8,None);name=U();self.call('glGenFramebuffers',None,[I,c.POINTER(U)],1,c.byref(name));self.fbo=name.value
        self.call('glBindFramebuffer',None,[U,U],0x8d40,self.fbo)
        self.call('glFramebufferTexture2D',None,[U,U,U,U,I],0x8d40,0x8ce0,0xde1,texture,0)
        require(self.call('glCheckFramebufferStatus',U,[U],0x8d40)==0x8cd5,'actual framebuffer incomplete')
        self.call('glViewport',None,[I,I,I,I],0,0,8,8)
        for cap in (0xb71,0xbe2,0xb44,0xc11,0xbd0):self.call('glDisable',None,[U],cap)
        require(self.error()==0,'setup GL error')

    def texture(self, width, height, data):
        """分配实际 texture；FBO attachment 与 sampler 纹理不同，避免非法反馈。"""
        name=U();self.call('glGenTextures',None,[I,c.POINTER(U)],1,c.byref(name));self.textures.append(name.value)
        self.call('glBindTexture',None,[U,U],0xde1,name.value)
        self.call('glTexImage2D',None,[U,I,I,I,I,I,U,U,P],0xde1,0,0x8058,width,height,0,0x1908,0x1401,data)
        self.call('glTexParameteri',None,[U,U,I],0xde1,0x2801,0x2600);self.call('glTexParameteri',None,[U,U,I],0xde1,0x2800,0x2600)
        return name.value

    def compile(self, shader, body):
        """真实 parse/compile verdict；合法失败状态与 GL 调用错误分别记录。"""
        source=C(body.encode());self.call('glShaderSource',None,[U,I,c.POINTER(C),c.POINTER(I)],shader,1,c.byref(source),None)
        self.call('glCompileShader',None,[U],shader);status=I();self.call('glGetShaderiv',None,[U,U,c.POINTER(I)],shader,0x8b81,c.byref(status))
        log=c.create_string_buffer(32768);self.call('glGetShaderInfoLog',None,[U,I,c.POINTER(I),P],shader,len(log),None,log)
        return {'status':status.value,'log':log.value.decode(errors='replace'),'error':self.error()}

    def link(self, program):
        """读最近 link 的状态与 InfoLog；不从是否画出像素反推 LINK_STATUS。"""
        self.call('glLinkProgram',None,[U],program);status=I();self.call('glGetProgramiv',None,[U,U,c.POINTER(I)],program,0x8b82,c.byref(status))
        log=c.create_string_buffer(32768);self.call('glGetProgramInfoLog',None,[U,I,c.POINTER(I),P],program,len(log),None,log)
        return {'status':status.value,'log':log.value.decode(errors='replace'),'error':self.error()}

    def program(self, fragment, pending=False):
        """同生产 shader/link 入口；pending 错只在 shader compile 之后、link 之前注入。"""
        program=self.call('glCreateProgram',U,[]);self.programs.append(program);stages=[]
        vertex='#version 400 core\nvoid main(){vec2 p=vec2(gl_VertexID==1?3.0:-1.0,gl_VertexID==2?3.0:-1.0);gl_Position=vec4(p,0,1);}'
        for kind,body in ((0x8b31,vertex),(0x8b30,fragment)):
            shader=self.call('glCreateShader',U,[U],kind);self.shaders.append(shader)
            stages.append(self.compile(shader,body));self.call('glAttachShader',None,[U,U],program,shader)
        if pending:self.call('glEnable',None,[U],0xdead)
        return program,self.shaders[-1],stages,self.link(program)

    def location(self, program, name):return self.call('glGetUniformLocation',I,[U,C],program,name.encode())
    def use(self, program):self.call('glUseProgram',None,[U],program)

    def pixel(self):
        """预清蓝再 draw/read，一次错误与像素并列；不接受遗留像素作为执行证据。"""
        self.call('glClearColor',None,[F,F,F,F],0,0,1,1);self.call('glClear',None,[U],0x4000);clear_error=self.error()
        self.call('glDrawArrays',None,[U,I,I],4,0,3);draw_error=self.error();self.call('glFinish',None,[])
        pixel=(c.c_ubyte*4)();self.call('glReadPixels',None,[I,I,I,I,U,U,P],4,4,1,1,0x1908,0x1401,pixel)
        return {'pixel':list(pixel),'clearError':clear_error,'drawError':draw_error,'readError':self.error()}

    def row(self, name, passed, **evidence):
        """必需场景只写一条完整记录；期望是独立数值/规范输入，不从当前库结果学习。"""
        self.rows.append({'case':name,'pass':bool(passed),**evidence})

    @staticmethod
    def color(sample, expected):
        return sample['pixel']==expected and all(sample[x]==0 for x in ('clearError','drawError','readError'))

    def run(self):
        """固定九项，覆盖资源、失败 query、current执行、退休与精度的冷/热边界。"""
        green=[0,255,0,255];red=[255,0,0,255]
        fragment='#version 400 core\nlayout(std140) uniform Colors{vec4 blockTint;};uniform sampler2D image0;uniform float gain;out vec4 selected;void main(){selected=texture(image0,vec2(.5))*blockTint*gain;}'
        program,shader,stages,linked=self.program(fragment);self.use(program)
        gain=self.location(program,'gain');sampler=self.location(program,'image0')
        self.call('glActiveTexture',None,[U],0x84c3);self.texture(1,1,(c.c_ubyte*4)(255,255,255,255))
        self.call('glUniform1i',None,[I,I],sampler,3);self.call('glUniform1f',None,[I,F],gain,1)
        block=self.call('glGetUniformBlockIndex',U,[U,C],program,b'Colors');self.call('glUniformBlockBinding',None,[U,U,U],program,block,2)
        name=U();self.call('glGenBuffers',None,[I,c.POINTER(U)],1,c.byref(name));buffer=name.value;self.buffers.append(buffer)
        self.call('glBindBuffer',None,[U,U],0x8a11,buffer);self.call('glBufferData',None,[U,Z,P,U],0x8a11,16,(F*4)(0,1,0,1),0x88e8)
        self.call('glBindBufferBase',None,[U,U,U],0x8a11,2,buffer);setup_error=self.error();baseline=self.pixel()
        self.row(CASES[0],all(x['status']==1 and x['error']==0 for x in stages) and linked['status']==1 and linked['error']==0 and gain>=0 and sampler>=0 and block!=0xffffffff and setup_error==0 and self.color(baseline,green),compile=stages,link=linked,gainLocation=gain,samplerLocation=sampler,blockIndex=block,samplerUnit=3,blockBinding=2,setupError=setup_error,pixel=baseline)
        failed_compile=self.compile(shader,'#version 400 core\ninvalid_body !!!');failed_link=self.link(program);current=I();self.call('glGetIntegerv',None,[U,c.POINTER(I)],0x8b8d,c.byref(current));query_error=self.error();failed_pixel=self.pixel()
        self.row(CASES[1],failed_compile['status']==0 and failed_link['status']==0 and bool(failed_link['log']) and failed_link['error']==0 and current.value==program and query_error==0 and self.color(failed_pixel,green),compile=failed_compile,link=failed_link,currentProgram=current.value,program=program,currentQueryError=query_error,pixel=failed_pixel)
        self.call('glUniform1f',None,[I,F],gain,2);old_setter=self.error();self.call('glProgramUniform1f',None,[U,I,F],program,gain,2);named_setter=self.error();failed_location=self.location(program,'gain');location_error=self.error();self.use(program);use_error=self.error()
        self.row(CASES[2],(old_setter,named_setter,location_error,use_error)==(1282,1282,1282,1282) and failed_location==-1,oldLocation=gain,oldSetterError=old_setter,namedSetterError=named_setter,failedLocation=failed_location,locationError=location_error,rebindError=use_error)
        self.call('glBindBuffer',None,[U,U],0x8a11,buffer);self.call('glBufferSubData',None,[U,Z,Z,P],0x8a11,0,16,(F*4)(1,0,0,1));resource_error=self.error();resource_pixel=self.pixel()
        self.row(CASES[3],resource_error==0 and self.color(resource_pixel,red),buffer=buffer,actualUpdate=[1,0,0,1],updateError=resource_error,pixel=resource_pixel)
        self.compile(shader,'#version 400 core\nsecond_invalid_body !!!');repeated=self.link(program);repeated_pixel=self.pixel()
        # 规范不要求 generic link log 复述非法源码标识符；原生只报 uncompiled shader。
        # 第二次不同 source 的公开 compile/link 调用仍在上面实际执行，最近失败必须
        # 有非空诊断且不丢 old current 资源，不能强迫参考实现采用 MGL 的日志措辞。
        self.row(CASES[4],repeated['status']==0 and bool(repeated['log']) and repeated['error']==0 and self.color(repeated_pixel,red),link=repeated,pixel=repeated_pixel,submittedFragment='second_invalid_body !!!')
        success_compile=self.compile(shader,fragment);success_link=self.link(program);gain2=self.location(program,'gain');sampler2=self.location(program,'image0');block2=self.call('glGetUniformBlockIndex',U,[U,C],program,b'Colors')
        self.call('glUniform1f',None,[I,F],gain2,1);self.call('glUniform1i',None,[I,I],sampler2,3);self.call('glUniformBlockBinding',None,[U,U,U],program,block2,2);success_error=self.error();success_pixel=self.pixel()
        self.call('glDeleteProgram',None,[U],program);deleted_pixel=self.pixel();self.use(0);retire_error=self.error();is_program=self.call('glIsProgram',U,[U],program);self.programs.remove(program)
        self.row(CASES[5],success_compile['status']==1 and success_link['status']==1 and success_error==0 and self.color(success_pixel,red) and self.color(deleted_pixel,red) and retire_error==0 and is_program==0,compile=success_compile,link=success_link,pixel=success_pixel,deleteWhileCurrentPixel=deleted_pixel,unbindError=retire_error,isProgramAfterUnbind=is_program)
        unused='#version 400 core\nuniform uint unusedInput;double unusedFunction(){return double(unusedInput);}out vec4 selected;void main(){selected=vec4(0,1,0,1);}'
        unused_program,_,unused_stages,unused_link=self.program(unused);self.use(unused_program);unused_use_error=self.error();unused_pixel=self.pixel()
        self.row(CASES[6],all(x['status']==1 and x['error']==0 for x in unused_stages) and unused_link['status']==1 and unused_link['error']==0 and unused_use_error==0 and self.color(unused_pixel,green),compile=unused_stages,link=unused_link,useError=unused_use_error,pixel=unused_pixel)
        dynamic='#version 400 core\nuniform uint a;uniform uint b;uniform uint expectedGap;out vec4 selected;void main(){double gap=double(a)-double(b);selected=gap==double(expectedGap)?vec4(0,1,0,1):vec4(1,0,0,1);}'
        dynamic_rows=[self.float64(dynamic,((16777217,16777216,1,green),(16777217,16777216,0,red))) for _ in range(3)]
        self.row(CASES[7],all(row['pass'] for row in dynamic_rows),attempts=dynamic_rows,scope='same-source cold + two warm link attempts; actual cache hit counters require separate production cache control')
        packed='#version 400 core\nuniform uint a;uniform uint b;uniform uint expectedGap;out vec4 selected;void main(){double d=packDouble2x32(uvec2(a,b));selected=d>double(expectedGap)?vec4(0,1,0,1):vec4(1,0,0,1);}'
        packed_row=self.float64(packed,((1,0x3ff00000,1,green),(0,0x3ff00000,1,red)),pending=True)
        self.row(CASES[8],packed_row['pass'],**{'observation':packed_row})

    def float64(self, fragment, samples, pending=False):
        """compile本身合法；reject模式要求最近link0/可定位log，native模式要求真实像素。"""
        program,_,stages,linked=self.program(fragment,pending=pending)
        expected_error=1280 if pending else 0;observations=[]
        passed=all(row['status']==1 and row['error']==0 for row in stages) and linked['error']==expected_error and self.error()==0
        if self.args.fp64=='reject':
            passed=passed and linked['status']==0 and 'Float64' in linked['log'] and 'GL_FRAGMENT_SHADER' in linked['log']
        elif linked['status']==1:
            self.use(program);locations=[self.location(program,name) for name in ('a','b','expectedGap')]
            passed=passed and min(locations)>=0 and self.error()==0
            for a,b,gap,color in samples:
                for location,value in zip(locations,(a,b,gap)):self.call('glUniform1ui',None,[I,U],location,value)
                actual=[]
                for location in locations:value=U();self.call('glGetUniformuiv',None,[U,I,c.POINTER(U)],program,location,c.byref(value));actual.append(value.value)
                input_error=self.error();pixel=self.pixel();passed=passed and actual==[a,b,gap] and input_error==0 and self.color(pixel,color)
                observations.append({'input':[a,b,gap],'actual':actual,'inputError':input_error,'pixel':pixel,'expected':color})
        else:passed=False
        return {'pass':bool(passed),'compile':stages,'link':linked,'expectedLinkError':expected_error,'samples':observations}

    def retire(self):
        """先解除GL资源/current，再按原EGL owner销毁；每项实际失败使整份报告不完整。"""
        self.use(0)
        for program in self.programs:self.call('glDeleteProgram',None,[U],program)
        for shader in self.shaders:self.call('glDeleteShader',None,[U],shader)
        for buffer in self.buffers:self.call('glDeleteBuffers',None,[I,c.POINTER(U)],1,c.byref(U(buffer)))
        for texture in self.textures:self.call('glDeleteTextures',None,[I,c.POINTER(U)],1,c.byref(U(texture)))
        if self.fbo:self.call('glDeleteFramebuffers',None,[I,c.POINTER(U)],1,c.byref(U(self.fbo)))
        if self.vao:self.call('glDeleteVertexArrays',None,[I,c.POINTER(U)],1,c.byref(U(self.vao)))
        require(self.error()==0,'resource retirement GL error')
        require(self.makecurrent(self.display,None,None,None)==1,'current retirement failed')
        require(self.egl('eglDestroyContext',U,[P,P])(self.display,self.context)==1,'context retirement failed')
        require(self.egl('eglDestroySurface',U,[P,P])(self.display,self.surface)==1,'surface retirement failed')
        require(self.egl('eglTerminate',U,[P])(self.display)==1,'display retirement failed')


def main():
    """先核独立DSO指纹与新报告路径，再执行一次冷进程；异常留下NOT_RUN完整边界。"""
    parser=argparse.ArgumentParser();parser.add_argument('--mode',choices=('reference','mobilegl'),required=True)
    parser.add_argument('--library',type=Path,required=True);parser.add_argument('--expected-library-sha',required=True)
    parser.add_argument('--fp64',choices=('native','reject'),required=True);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    if args.report.exists():raise RuntimeError('refusing existing report')
    actual=hashlib.sha256(args.library.read_bytes()).hexdigest()
    if actual!=args.expected_library_sha:raise RuntimeError('requested library does not match independent expected SHA')
    result={'startedAt':datetime.now(timezone.utc).isoformat(),'pid':os.getpid(),'library':str(args.library.resolve()),
            'librarySha256':actual,'mode':args.mode,'fp64Expected':args.fp64,'backendRequested':os.getenv('MOBILEGL_BACKEND_TYPE'),
            'scope':'fixed nine host contracts; reject is a precision limitation, not Core4/OHOS qualification'}
    scene=None
    try:
        scene=Scene(args);scene.setup();scene.run();scene.retire()
        result.update(completed=True,retired=True,version=scene.version,renderer=scene.renderer,bindings=scene.bindings,cases=scene.rows)
        result['passed']=tuple(row['case'] for row in scene.rows)==CASES and all(row['pass'] for row in scene.rows)
    except Exception as error:
        result.update(completed=False,passed=False,error=type(error).__name__+': '+str(error),
                      bindings=scene.bindings if scene else [],cases=scene.rows if scene else [])
    result['finishedAt']=datetime.now(timezone.utc).isoformat();args.report.parent.mkdir(parents=True,exist_ok=True)
    with args.report.open('x',encoding='utf-8') as output:json.dump(result,output,indent=2)
    print(json.dumps({key:value for key,value in result.items() if key not in ('bindings','cases')}))
    return 0 if result['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
