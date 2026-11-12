"""执行真实renderer写前屏障函数，核失败/无工作/view/提交顺序；GPU语义由独立设备像素测试证明。"""
from pathlib import Path
import argparse,hashlib,json,subprocess
from datetime import datetime

p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=False)
source=root/'MobileGL/MG_Backend/DirectVulkan/Renderer/VulkanRenderer.cpp';data=source.read_text()
start=data.index('    Bool VulkanRenderer::PrepareTextureCpuWrite(');end=data.index('\n    Bool VulkanRenderer::MaterializePendingClearForTexture(',start)
function=data[start:end]
# 只替换渲染基础设施，待测函数逐字来自当前生产文件，不用Python重写算法。
prefix=r'''
#include <vector>
#include <memory>
#include <string>
#include <stdexcept>
#include <iostream>
using Bool=bool; template<class T>using Vector=std::vector<T>;
static std::string trace;
namespace MG_State::GLState {struct ITextureObject {std::shared_ptr<ITextureObject> owner;const auto& GetViewStorageOwner()const{return owner;}};}
struct PendingClearEntry{};
struct Frame{bool isCommandRecording=false,hasCommandBufferRecorded=false;int commandBuffer=1;};
struct Frames{int count=1;Frame frame;int GetFrameCount()const{return count;}Frame& GetCurrent(){return frame;}void BeginCommandRecording(){trace+='B';frame.isCommandRecording=true;}};
struct Clears{bool pending=false;MG_State::GLState::ITextureObject* queried=nullptr;bool GetPendingClears(MG_State::GLState::ITextureObject* t,Vector<PendingClearEntry>& v){queried=t;if(pending)v.push_back({});return pending;}};
struct VkRenderPassManager{static inline bool active=false;static void* GetActiveRenderPass(){return active?reinterpret_cast<void*>(1):nullptr;}static void EndRenderPass(int){trace+='E';active=false;}};
struct VulkanRenderer{
 Clears clear;Frames m_frameContext;Clears* m_clearManager=&clear;void* m_textureManager=this;
 bool materializeOk=true,flushOk=true,m_lastSampledSetValid=true;MG_State::GLState::ITextureObject* materialized=nullptr;
 bool HasPendingRecordedWork(){auto& f=m_frameContext.frame;return f.isCommandRecording||f.hasCommandBufferRecorded;}
 bool MaterializePendingClearForTexture(int,MG_State::GLState::ITextureObject& t){trace+='M';materialized=&t;return materializeOk;}
 bool FlushPendingCommands(){trace+='F';if(flushOk){m_frameContext.frame.isCommandRecording=false;m_frameContext.frame.hasCommandBufferRecorded=false;}return flushOk;}
 Bool PrepareTextureCpuWrite(MG_State::GLState::ITextureObject& texture);
};
'''
suffix=r'''
static void require(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
int main(){
 MG_State::GLState::ITextureObject texture;
 for(int n=0;n<11;n++){
  VulkanRenderer r;trace.clear();VkRenderPassManager::active=false;
  if(n==0){require(r.PrepareTextureCpuWrite(texture)&&trace.empty(),"idle must not allocate/submit");}
  if(n==1){r.clear.pending=true;require(r.PrepareTextureCpuWrite(texture)&&trace=="BMF"&&!r.m_lastSampledSetValid,"old clear before submit");}
  if(n==2){r.clear.pending=true;r.materializeOk=false;require(!r.PrepareTextureCpuWrite(texture)&&trace=="BM","failed materialize refuses write");}
  if(n==3){r.clear.pending=true;r.flushOk=false;require(!r.PrepareTextureCpuWrite(texture)&&trace=="BMF","failed submit refuses write");}
  if(n==4){r.m_frameContext.frame.isCommandRecording=true;require(r.PrepareTextureCpuWrite(texture)&&trace=="F","old draw must submit");}
  if(n==5){r.clear.pending=true;r.m_frameContext.frame.isCommandRecording=true;VkRenderPassManager::active=true;require(r.PrepareTextureCpuWrite(texture)&&trace=="EMF","end active render pass before clear");}
  if(n==6){MG_State::GLState::ITextureObject view;view.owner=std::make_shared<MG_State::GLState::ITextureObject>();r.clear.pending=true;require(r.PrepareTextureCpuWrite(view)&&r.materialized==view.owner.get()&&r.clear.queried==view.owner.get(),"view resolves storage once");}
  if(n==7){r.clear.pending=true;r.m_frameContext.count=0;require(!r.PrepareTextureCpuWrite(texture)&&trace.empty(),"no frame refuses pending clear");}
  if(n==8){r.m_textureManager=nullptr;require(!r.PrepareTextureCpuWrite(texture)&&trace.empty(),"missing backend refuses write");}
  if(n==9){r.clear.pending=true;r.m_frameContext.frame.hasCommandBufferRecorded=true;r.flushOk=false;require(!r.PrepareTextureCpuWrite(texture)&&trace=="F"&&r.m_frameContext.frame.hasCommandBufferRecorded,"failed previous submit must not reset old buffer");}
  if(n==10){r.clear.pending=true;r.m_frameContext.frame.hasCommandBufferRecorded=true;require(r.PrepareTextureCpuWrite(texture)&&trace=="FBMF","resubmit ended old buffer before recording later clear");}
 }
 std::cout<<"texture write barrier: 11 production-function cases passed\n";
}
'''
def linux(path):
 q=Path(path).as_posix();return '/mnt/'+q[0].lower()+q[2:]
receipt={'at':datetime.now().astimezone().isoformat(),'source':str(source),'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'production renderer method with explicit frame/clear/submit test doubles; not full GPU or GL entry point qualification','cases':11,'passed':False,'steps':[]}
try:
 for label,body,expect in [('candidate',function,0),('removed-submit-negative',function.replace('if (!FlushPendingCommands()) return false;','/* negative: deliberately omit submission */'),None)]:
  cpp=out/(label+'.cpp');exe=out/(label+'.exe');cpp.write_text(prefix+body+suffix,encoding='utf-8',newline='\n')
  compile=subprocess.run(['wsl.exe','-d','Ubuntu','--','g++','-std=c++20','-O1','-g',linux(cpp),'-o',linux(exe)],capture_output=True)
  (out/(label+'-compile.log')).write_bytes(compile.stdout+compile.stderr);assert compile.returncode==0
  run=subprocess.run(['wsl.exe','-d','Ubuntu','--',linux(exe)],capture_output=True);(out/(label+'.log')).write_bytes(run.stdout+run.stderr)
  receipt['steps'].append({'case':label,'exitCode':run.returncode});assert run.returncode==0 if expect==0 else run.returncode!=0
  exe.unlink()
 receipt['passed']=True
finally:(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt))
