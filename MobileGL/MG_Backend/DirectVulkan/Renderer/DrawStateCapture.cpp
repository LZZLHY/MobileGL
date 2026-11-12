// SPDX-License-Identifier: LGPL-3.0-only
// 显式启用的开发诊断：从实际uniform绑定入口复制CPU状态，定位完整整合包
// 与独立GPU用例之间的差异。默认关闭；不改变任何GL状态、不等待GPU。
// 每进程只接纳首个调用线程，匹配指定uniform名称后跳过预热绑定，最多64条、
// 4MiB。记录的是尝试，不保证提交或呈现；未凑足窗口便退出时可能没有文件。
#include "DrawStateCapture.h"
#include "Config.h"
#include <MG_State/GLState/Core.h>
#include <MG_State/GLState/ProgramState/ShaderCompileTask.h>
#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <locale>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace MobileGL::MG_Backend::DirectVulkan {
    namespace {
        constexpr SizeT kByteBudget = 4 * 1024 * 1024;
        constexpr SizeT kUboBudget = 16 * 1024;
        // 字符串拥有全部快照字节，不保留对象裸指针。锁仅保护诊断本身；不能
        // 借它宣称其他线程共享GLContext已经安全。停止标记亦覆盖异常退出路径。
        struct CaptureState {
            std::mutex mutex;
            std::thread::id owner;
            Bool claimed = false;
            std::atomic_bool stopped{false};
            Uint64 attempts = 0, matches = 0;
            Uint32 records = 0;
            String bytes;
            std::set<std::pair<Uint64, Uint32>> dictionaries;
        };
        CaptureState& State() { static CaptureState value; return value; }

        // JSON只记录选定shader和技术状态，不读取环境全表或应用文件。控制字符严格转义。
        void Quote(std::ostream& out, const String& value) {
            constexpr char hex[] = "0123456789abcdef";
            out << '"';
            for (unsigned char c : value) {
                if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
                else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
                else out << static_cast<char>(c);
            }
            out << '"';
        }
        // 非有限浮点数用字符串表达，避免生成非法JSON或掩盖异常uniform值。
        void Number(std::ostream& out, Float value) {
            if (std::isfinite(value)) out << std::setprecision(9) << value;
            else Quote(out, std::isnan(value) ? "nan" : value > 0 ? "+inf" : "-inf");
        }
        void Hex(std::ostream& out, const void* data, SizeT size) {
            constexpr char digits[] = "0123456789abcdef";
            out << '"';
            const auto* p = static_cast<const Uint8*>(data);
            for (SizeT i = 0; i < size; ++i) out << digits[p[i] >> 4] << digits[p[i] & 15];
            out << '"';
        }
        // 保留对象身份、版本及采样参数，区分纹理自身状态与sampler覆盖。
        void Sampler(std::ostream& out, const SharedPtr<MG_State::GLState::SamplerObject>& sampler) {
            if (!sampler) { out << "null"; return; }
            const auto& p = sampler->GetAllSamplerParameters();
            out << "{\"name\":" << sampler->GetExternalIndex() << ",\"life\":" << sampler->GetLifetimeId()
                << ",\"version\":" << sampler->GetVersion() << ",\"min\":" << Int(p.minFilter)
                << ",\"mag\":" << Int(p.magFilter) << ",\"mip\":" << Int(p.mipmapMode)
                << ",\"wrap\":[" << Int(p.wrapS) << ',' << Int(p.wrapT) << ',' << Int(p.wrapR)
                << "],\"compare\":[" << Int(p.compareMode) << ',' << Int(p.compareFunc) << "],\"lod\":[";
            Number(out,p.minLod);out<<',';Number(out,p.maxLod);out<<',';Number(out,p.lodBias);out<<"],\"anisotropy\":";Number(out,p.maxAnisotropy);out<<'}';
        }
        // 只读已存在的元数据；严禁为抓取调用纹理物化、上传或GPU读回。
        void Texture(std::ostream& out, const SharedPtr<MG_State::GLState::ITextureObject>& texture) {
            if (!texture) { out << "null"; return; }
            const auto size = texture->GetBaseSize(); const auto range = texture->GetLevelRange();
            const auto& owner = texture->GetViewStorageOwner();
            out << "{\"name\":" << texture->GetExternalIndex() << ",\"life\":" << texture->GetLifetimeId()
                << ",\"target\":" << Int(texture->GetTarget()) << ",\"format\":" << Int(texture->GetFormat())
                << ",\"size\":[" << size.x() << ',' << size.y() << ',' << size.z() << "],\"levels\":[" << range.x() << ',' << range.y()
                << "],\"content\":" << texture->GetContentVersion() << ",\"shape\":" << texture->GetShapeVersion()
                << ",\"params\":" << texture->GetTextureParamsVersion() << ",\"viewOwnerLife\":" << (owner ? owner->GetLifetimeId() : 0)
                << ",\"view\":[" << texture->GetViewMinLevel() << ',' << texture->GetViewNumLevels() << ',' << texture->GetViewMinLayer() << ',' << texture->GetViewNumLayers()
                << "],\"ownedSampler\":"; Sampler(out,texture->GetSamplerObject());out<<'}';
        }
        // texture附件保留完整绑定身份；renderbuffer当前仅标记类型，不能据此
        // 还原其存储内容或最终Vulkan attachment布局。
        void Framebuffer(std::ostream& out, const SharedPtr<MG_State::GLState::FramebufferObject>& fbo) {
            if (!fbo) { out << "null"; return; }
            out << "{\"name\":" << fbo->GetExternalIndex() << ",\"life\":" << fbo->GetLifetimeId()
                << ",\"version\":" << fbo->GetObjectVersion() << ",\"readBuffer\":" << Int(fbo->GetReadBuffer()) << ",\"attachments\":[";
            Bool first = true; const auto& attachments = fbo->GetAllAttachmentObjects();
            for (SizeT i=0;i<attachments.size();++i) {
                const auto& a=attachments[i];if(a.IsEmpty())continue;if(!first)out<<',';first=false;
                out<<"{\"slot\":"<<i<<",\"level\":"<<a.GetTextureLevel()<<",\"layer\":"<<a.GetTextureLayer()<<",\"face\":"<<Int(a.GetTextureUploadTarget())<<",\"texture\":";
                Texture(out,a.IsTexture()?a.GetTexture():nullptr);out<<",\"renderbuffer\":"<<(a.IsRenderbuffer()?"true":"false")<<'}';
            }
            out<<"],\"drawBuffers\":[";first=true;for(auto b:fbo->GetDrawBuffers()){if(!first)out<<',';first=false;out<<Int(b);}out<<"]}";
        }
        // 使用link后的真实uniform查询定位程序，不硬编码任何整合包或程序号。
        Bool Matches(const MG_State::GLState::ProgramObject& program) {
            const String& filters=MG_Config::Features.DrawCaptureMatch;
            SizeT start=0;
            for(Uint i=0;i<8 && start<filters.size();++i){const auto end=filters.find(',',start);const auto name=filters.substr(start,end==String::npos?String::npos:end-start);if(!name.empty()&&program.GetUniformLocation(name)>=0)return true;if(end==String::npos)break;start=end+1;}
            return false;
        }
        // 仅在窗口结束后一次落盘，独占创建拒绝覆盖旧证据。失败不影响正常GL调用。
        void Finish(CaptureState& state, const char* reason) {
            state.stopped=true;
            std::ostringstream tail;tail<<"{\"kind\":\"end\",\"reason\":\""<<reason<<"\",\"records\":"<<state.records<<",\"bindAttempts\":"<<state.attempts<<",\"matched\":"<<state.matches<<",\"completeFrames\":false}\n";
            state.bytes+=tail.str();
            FILE* file=std::fopen(MG_Config::Features.DrawCapturePath.c_str(),"wx");
            if(!file){MGLOG_W("DRAW_CAPTURE could not exclusively create output");return;}
            const auto written=std::fwrite(state.bytes.data(),1,state.bytes.size(),file);const int closed=std::fclose(file);
            MGLOG_I("DRAW_CAPTURE end records=%u bytes=%zu written=%zu close=%d reason=%s",state.records,state.bytes.size(),written,closed,reason);
        }
    }

    void CaptureProgramBinding(const MG_State::GLState::ProgramObject& program,
                               const ProgramFactory::VkProgramObject& variant,
                               Uint32 frameSlot, VkPipelineBindPoint bindPoint) noexcept {
        const auto& config=MG_Config::Features;
        if(!config.DrawCapture || config.DrawCapturePath.empty() || config.DrawCaptureMatch.empty() || config.DrawCaptureMatch.size()>512)return;
        try {
            auto& state=State();const std::lock_guard<std::mutex> guard(state.mutex);
            if(state.stopped)return;
            if(!state.claimed){state.owner=std::this_thread::get_id();state.claimed=true;}
            if(state.owner!=std::this_thread::get_id())return;
            ++state.attempts;if(!Matches(program))return;++state.matches;
            if(state.matches==1)MGLOG_I("DRAW_CAPTURE first match program=%u skip=%u count=%u",program.GetExternalIndex(),config.DrawCaptureSkip,config.DrawCaptureCount);
            if(state.matches<=config.DrawCaptureSkip)return;
            if(state.bytes.empty())state.bytes="{\"kind\":\"header\",\"schema\":\"mobilegl-cpu-binding-capture-v1\",\"scope\":\"selected uniform-bind attempts; no GPU readback or complete-frame claim\"}\n";
            std::ostringstream out;out.imbue(std::locale::classic());
            auto& context=*MG_State::pGLContext;
            out<<"{\"kind\":\"uniform_bind_attempt\",\"sequence\":"<<state.attempts<<",\"epochMs\":"<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
               <<",\"frameSlot\":"<<frameSlot<<",\"bindPoint\":"<<Int(bindPoint)<<",\"program\":"<<program.GetExternalIndex()<<",\"life\":"<<program.GetLifetimeId()<<",\"link\":"<<program.GetLinkVersion()<<",\"uboVersion\":"<<program.GetUBOContentVersion()<<",\"variantHash\":"<<variant.hash;
            const auto key=std::make_pair(program.GetLifetimeId(),program.GetLinkVersion());
            if(state.dictionaries.insert(key).second){
                out<<",\"linkedShaderCount\":"<<program.GetLinkedShaderSnapshot().size()<<",\"linkedShaders\":[";Bool first=true;Uint shaderCount=0;
                for(const auto& ref:program.GetLinkedShaderSnapshot()){
                    if(shaderCount++>=8)break;
                    if(!first)out<<',';first=false;out<<"{\"stage\":"<<(ref.shader?Int(ref.shader->GetShaderStage()):-1)<<",\"sourceHash\":"<<(ref.node?ref.node->sourceHash:0)<<",\"source\":";
                    if(ref.source && ref.source->size()<=65536)Quote(out,*ref.source);else out<<"null";out<<'}';
                }out<<']';
            }
            out<<",\"modules\":[";Bool first=true;for(const auto& digest:variant.stageSpirvDigests){if(!first)out<<',';first=false;out<<"["<<digest.stage<<','<<digest.wordCount<<','<<digest.hash<<']';}out<<']';
            const auto uboBytes=program.GetUBOSize();const auto* uboData=program.GetUBOData();out<<",\"uboSize\":"<<uboBytes<<",\"uboHex\":";if(uboData)Hex(out,uboData,std::min<SizeT>(uboBytes,kUboBudget));else out<<"null";
            out<<",\"uniforms\":[";first=true;
            for(Uint i=0;i<std::min<Uint>(program.GetUniformCount(),128);++i){
                const auto& name=program.GetActiveUniformName(i);const Int location=program.GetUniformLocation(name);if(!program.IsValidUniformLocation(location))continue;
                const Bool opaque=program.IsUniformOpaqueAtLocation(location);const auto offset=opaque?MG_State::GLState::ProgramObject::kInvalidUniformOffset:program.GetUniformOffset(location);
                const SizeT span=offset!=MG_State::GLState::ProgramObject::kInvalidUniformOffset?program.GetUniformStorageSpanInBytes(location):0;
                if(!first)out<<',';first=false;out<<"{\"name\":";Quote(out,name);out<<",\"index\":"<<i<<",\"location\":"<<location<<",\"type\":"<<program.GetUniformType(location)<<",\"opaque\":"<<(opaque?"true":"false")<<",\"offset\":"<<offset<<",\"span\":"<<span<<'}';
            }out<<"],\"samplerBindings\":[";first=true;
            for(auto binding:variant.activeBindings){
                if(binding>=variant.bindingKinds.size() || variant.bindingKinds[binding]!=ProgramFactory::DescriptorBindingKind::CombinedImageSampler)continue;
                if(binding>=variant.samplerUniformLocationByBinding.size() || binding>=variant.samplerTextureTargetByBinding.size())continue;
                const auto location=variant.samplerUniformLocationByBinding[binding];if(!program.IsValidUniformLocation(location))continue;
                if(!first)out<<',';first=false;const auto unit=program.GetUniformSamplerOrImageUnitIndex(location);out<<"{\"binding\":"<<binding<<",\"location\":"<<location<<",\"unit\":"<<unit<<",\"element0Only\":true,\"texture\":";
                const auto target=variant.samplerTextureTargetByBinding[binding];
                if(unit>=0 && unit<=context.GetMaxTouchedTextureUnit() && target!=TextureTarget::Unknown && target<TextureTarget::TextureTargetCount) {auto& t=context.GetTextureUnitObject(unit);Texture(out,t.GetBindingSlot(target).GetBoundObject());out<<",\"overrideSampler\":";Sampler(out,t.GetSamplerObject());}else out<<"null";
                out<<'}';
            }out<<"],\"readFbo\":";Framebuffer(out,context.GetFramebufferBindingSlot(FramebufferTarget::Read).GetBoundObject());out<<",\"drawFbo\":";Framebuffer(out,context.GetFramebufferBindingSlot(FramebufferTarget::Draw).GetBoundObject());
            const auto v=context.GetViewportIndexed(0);out<<",\"viewport\":[";Number(out,v.x());out<<',';Number(out,v.y());out<<',';Number(out,v.z());out<<',';Number(out,v.w());out<<']';
            out<<",\"depthTest\":"<<(context.IsCapabilityEnabled(CapabilityInput::DepthTest)?"true":"false")<<",\"depthWrite\":"<<(context.GetDepthMask()?"true":"false")<<",\"depthFunc\":"<<Int(context.GetDepthFunc())<<",\"blend\":"<<(context.IsCapabilityEnabled(CapabilityInput::Blend)?"true":"false")<<",\"partialUniforms\":"<<((program.GetUniformCount()>128||uboBytes>kUboBudget)?"true":"false")<<"}\n";
            const String record=out.str();if(record.size()>kByteBudget-512-state.bytes.size()){Finish(state,"byte-budget");return;}state.bytes+=record;++state.records;
            if(state.records>=config.DrawCaptureCount)Finish(state,"sample-count");
        } catch (...) {
            // 诊断分配/序列化失败不得穿过C ABI或改变游戏结果；后续不再重试申请内存。
            State().stopped=true;MGLOG_W("DRAW_CAPTURE stopped after diagnostic exception");
        }
    }
}
