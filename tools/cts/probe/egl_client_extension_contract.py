#!/usr/bin/env python3
"""真实库的EGL客户端声明回归；每次调用是独立冷进程，输出须显式仓外。

不构造不存在的EXT surface stub。完整广告必须有三个规范入口；当前窄修应撤除广告。
core1.5地址/错误/pbuffer另测，不能把其存在或pixmap状态句柄当EXT功能资格。
"""
from pathlib import Path
from datetime import datetime,timezone
import argparse,ctypes,hashlib,json,os

def client_contract_issues(tokens, entries):
 """校验扩展广告的proc合同；direct ELF export仅观测，不是扩展规范必需条件。"""
 issues=[]
 if 'EGL_EXT_platform_base' in tokens and any(not x['proc'] for x in entries):issues.append('advertised EXT_platform_base lacks one or more required proc commands')
 if 'EGL_KHR_platform_base' in tokens:issues.append('KHR_platform_base has no official Registry extension identity')
 return issues

def main():
 """以明确冷库和仓外新report为输入/输出；同源绑定失败或不满足合同均不能PASS。"""
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--library',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--expect-withdrawn',action='store_true');args=p.parse_args()
 library=args.library.resolve();output=args.output.resolve();checkout=Path(__file__).resolve().parents[3]
 if output==checkout or checkout in output.parents:raise ValueError('Report must be outside source checkout')
 if output.exists():raise FileExistsError('Refusing existing report: '+str(output))
 started=datetime.now(timezone.utc).isoformat();lib=ctypes.CDLL(str(library));rows=[];failures=[]
 bindings=[]
 class DlInfo(ctypes.Structure):
  _fields_=[('filename',ctypes.c_char_p),('base',ctypes.c_void_p),('symbol',ctypes.c_char_p),('address',ctypes.c_void_p)]
 runtime=ctypes.CDLL(None);runtime.dladdr.argtypes=[ctypes.c_void_p,ctypes.POINTER(DlInfo)];runtime.dladdr.restype=ctypes.c_int
 def owner(address,name,kind,required=True):
  """核requested映像：required用于实际调用/proc并拒foreign；optional导出只记录归属不授予功能。"""
  info=DlInfo();resolved=runtime.dladdr(ctypes.c_void_p(address),ctypes.byref(info));actual=Path(os.fsdecode(info.filename)).resolve() if resolved and info.filename else None
  bindings.append({'name':name,'kind':kind,'address':address,'owner':str(actual) if actual else None,'imageBase':info.base,'required':required,'requestedImage':actual==library})
  if required and actual!=library:raise ValueError('FOREIGN bound function '+name+': '+str(actual)+' != requested '+str(library))
 def bind(name,result,types):
  """绑定实际dlsym入口并核canonical dladdr owner，防依赖库借名；不更改返回/错误账本。"""
  function=getattr(lib,name);owner(ctypes.cast(function,ctypes.c_void_p).value,name,'used export')
  function.restype=result;function.argtypes=types;return function
 pointer=ctypes.c_void_p;integer=ctypes.c_int;boolean=ctypes.c_uint;attribute=ctypes.c_ssize_t
 query=bind('eglQueryString',ctypes.c_char_p,[pointer,integer]);get_proc=bind('eglGetProcAddress',pointer,[ctypes.c_char_p]);get_error=bind('eglGetError',integer,[])
 get_display=bind('eglGetDisplay',pointer,[pointer]);initialize=bind('eglInitialize',boolean,[pointer,ctypes.POINTER(integer),ctypes.POINTER(integer)])
 choose=bind('eglChooseConfig',boolean,[pointer,ctypes.POINTER(integer),ctypes.POINTER(pointer),integer,ctypes.POINTER(integer)])
 create_pbuffer=bind('eglCreatePbufferSurface',pointer,[pointer,pointer,ctypes.POINTER(integer)]);query_surface=bind('eglQuerySurface',boolean,[pointer,pointer,integer,ctypes.POINTER(integer)])
 destroy_surface=bind('eglDestroySurface',boolean,[pointer,pointer]);terminate=bind('eglTerminate',boolean,[pointer])
 core_window=bind('eglCreatePlatformWindowSurface',pointer,[pointer,pointer,pointer,ctypes.POINTER(attribute)]);core_pixmap=bind('eglCreatePlatformPixmapSurface',pointer,[pointer,pointer,pointer,ctypes.POINTER(attribute)])
 client=query(None,0x3055).decode();tokens=set(client.split());declared='EGL_EXT_platform_base' in tokens
 entries=[]
 for name in ['eglGetPlatformDisplayEXT','eglCreatePlatformWindowSurfaceEXT','eglCreatePlatformPixmapSurfaceEXT']:
  resolved=get_proc(name.encode());export=None
  try:export=ctypes.cast(getattr(lib,name),pointer).value
  except AttributeError:pass
  if resolved:owner(resolved,name,'extension proc')
  if export:owner(export,name,'observed direct export',required=False)
  entries.append({'name':name,'proc':resolved,'export':export,'targetInvocation':'NOT_RUN: no window/pixmap EXT route qualified' if not resolved else 'address only; not semantic qualification','attributeAbi':'EGLint* (32-bit)'} )
 failures.extend(client_contract_issues(tokens,entries))
 if args.expect_withdrawn and declared:failures.append('candidate still advertises withdrawn extension')
 # 查询/取地址不应吞原EGL错误。仅无效query设置错误；无NULL函数调用。
 query(None,0xdead);query(None,0x3055);get_proc(b'eglCreatePlatformWindowSurfaceEXT');pending=get_error();rows.append({'case':'pending-error-query','actual':pending,'expected':0x300c});
 if pending!=0x300c:failures.append('metadata query swallowed pending EGL error')
 display=get_display(None);major=integer();minor=integer();assert initialize(display,ctypes.byref(major),ctypes.byref(minor))==1
 config=pointer();count=integer();attrs=(integer*3)(0x3033,0x1,0x3038);assert choose(display,attrs,ctypes.byref(config),1,ctypes.byref(count))==1 and count.value>0
 empty64=(attribute*1)(0x3038)
 for name,function,error in [('core-window-null',core_window,0x300b),('core-pixmap-null',core_pixmap,0x300a)]:
  get_error();value=function(display,config,None,empty64);actual=get_error();rows.append({'case':name,'surface':value,'error':actual,'expectedError':error,'attributeAbi':'EGLAttrib* (pointer-sized)','semanticScope':'legal failure path only; no real native window/pixmap'});
  if value or actual!=error:failures.append(name+' did not return the existing precise no-surface error')
 # 显式32-bit EGLint成对属性通过真实pbuffer路径，宽/高不同可检出错误宽度解释。
 pbuffer_attrs=(integer*5)(0x3057,3,0x3056,7,0x3038);surface=create_pbuffer(display,config,pbuffer_attrs);error=get_error();width=integer();height=integer()
 if not surface:failures.append('actual pbuffer creation failed: '+hex(error))
 else:
  assert query_surface(display,surface,0x3057,ctypes.byref(width))==1 and query_surface(display,surface,0x3056,ctypes.byref(height))==1
  if (width.value,height.value)!=(3,7):failures.append('32-bit pbuffer attributes did not preserve 3x7')
  assert destroy_surface(display,surface)==1
 rows.append({'case':'actual-pbuffer','width':width.value,'height':height.value,'created':bool(surface),'creationError':error,'attributeAbi':'EGLint* (32-bit)'})
 # 不调用EXT两个NULL入口，也不把Core pixmap仅分配状态句柄误报功能；此缺陷另行治理。
 assert terminate(display)==1
 result={'startedAt':started,'finishedAt':datetime.now(timezone.utc).isoformat(),'pid':os.getpid(),'backendRequested':os.environ.get('MOBILEGL_BACKEND_TYPE'),'library':str(library),'librarySha256':hashlib.sha256(library.read_bytes()).hexdigest(),'bindings':bindings,'typeSizes':{'EGLint':ctypes.sizeof(integer),'EGLAttrib':ctypes.sizeof(attribute)},'clientExtensions':client,'extensionDeclared':declared,'entries':entries,'coreChecks':rows,'terminated':True,'failures':failures,'passed':not failures,'scope':'declared client interface plus legal core no-surface/pbuffer only; no EXT window/pixmap implementation qualification'}
 output.parent.mkdir(parents=True,exist_ok=True)
 with output.open('x',encoding='utf8') as report:report.write(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result));return 0 if result['passed'] else 1
if __name__=='__main__':
 try:raise SystemExit(main())
 except Exception as error:print('EGL interface NOT_RUN/ERROR:',type(error).__name__,str(error));raise SystemExit(2)
