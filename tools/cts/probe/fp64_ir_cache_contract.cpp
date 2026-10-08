// Float64/current 候选的宿主 CPU 合同：驱动真实 glslang、optimizer、ProgramObject 与 L1。
// 这里只证明 typed IR/缓存 namespace/异步对象寿命，不宣称 GPU 或 OHOS 资格；
// 生产库另由固定九项同映像像素场景验证。失败状态/返回值由原生产对象直接读取。
#include "Init.h"
#include "Config.h"
#include "MG_State/GLState/ProgramState/ShaderObject.h"
#include "MG_State/GLState/ProgramState/ProgramObject.h"
#include "MG_State/GLState/ProgramState/ProgramTranslationCache.h"
#include "MG_Util/ShaderTranspiler/ShaderCompiler.h"
#include "MG_Util/ShaderTranspiler/CompileEnv.h"
#include "MG_Util/Async/ShaderCompilePool.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <filesystem>
#include <spirv-tools/libspirv.hpp>

using namespace MobileGL;
using namespace MobileGL::MG_State::GLState;
using namespace MobileGL::MG_Util::ShaderTranspiler;

namespace {
    int failures = 0;
    void Check(const char* name, Bool passed) {
        // 每个控制独立记录，错误不被后续成功覆盖；末尾计数才形成完整退出边界。
        std::cout << "CPU_CHECK " << name << " pass=" << passed << '\n';
        if (!passed) ++failures;
    }

    SharedPtr<ShaderObject> Shader(const String& source, ShaderStage stage, Uint name) {
        // 生产编译对象保存实际预处理文本及 AST；不模拟其 parse 或反射算法。
        auto shader = MakeShared<ShaderObject>(stage, name);
        shader->SetShaderSource(source);shader->Compile();
        if (!shader->GetCompileStatus()) throw std::runtime_error(shader->GetInfoLog());
        return shader;
    }

    SharedPtr<ProgramObject> Program(const SharedPtr<ShaderObject>& shader, Uint name) {
        // 默认 framebuffer/driver 未加入此 CPU fixture。Link(false) 是已有对象入口，
        // 固定 maxFragmentOutput=8，读取真实环境快照，随后 join 原 A/B 任务。
        auto program = MakeShared<ProgramObject>(name);program->AttachShader(shader);
        program->SetMaxFragmentOutputColorNumber(8);program->Link(false);program->JoinLinkAndSpirv();
        return program;
    }

    TranslationCacheKey ExactKey(const SharedPtr<ShaderObject>& shader) {
        // key builder 仍用生产函数；输入来自这个真实 shader 的 terminal 编译 artifacts。
        // 先通过真实成功 L1 Find 校准元组，任何输入遗漏会使校准失败，不能算 epoch 通过。
        const auto& node=shader->CompiledNodeForLink();
        const auto& env=GetCurrentCompileEnv();
        SpirvTranslationKeyInputs input;
        input.frontendFingerprint=env->frontendFingerprint;
        input.nativeFloat64=env->ConsumesFloat64Natively();
        input.enableSpirvValidation=MG_Config::Features.EnableSpirvValidation;
        input.stages.push_back({GL_FRAGMENT_SHADER,node->artifacts.preprocessedSource});
        input.xfbBufferMode=GL_INTERLEAVED_ATTRIBS;input.maxFragmentOutputColorNumber=8;
        return BuildSpirvTranslationKey(input);
    }

    Vector<Uint32> Binary(GLenum stage, const String& source) {
        // 和已有 DemoteFloat64Test 相同入口，但判据来自本实验明确的 storage 副作用。
        auto shader=ShaderCompiler::CompileShader(ShaderAttrib{.shaderType=stage,.sourceStr=source});
        if (!shader) throw std::runtime_error(shader.error().log);
        auto program=ShaderCompiler::LinkProgram(ProgramAttrib{.shaders={shader.value()}});
        if (!program) throw std::runtime_error(program.error().log);
        auto binary=ShaderCompiler::GetSpirvBinaryFromProgram(ProgramBinaryAttrib{.shaderTypes={stage},.program=*program.value()});
        if (!binary || binary->size()!=1) throw std::runtime_error("expected one actual module");
        Vector<Uint32> optimized;
        if (!ShaderCompiler::SanitizeAndOptimizeBinary(binary->front(),optimized,true,true,true))
            throw std::runtime_error("actual optimizer failed");
        return optimized;
    }

    Vector<Uint32> QualifiedAssembly(const char* name, const char* source, const std::filesystem::path& out) {
        // 真实SPIRV-Tools校验+生产安全优化；留下优化后文本/字节，不能凭类型命名猜需求。
        spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_1);Vector<Uint32> raw, optimized;String text;
        tools.SetMessageConsumer([](auto, const char*, const auto& position, const char* message) {
            std::cerr << "SPIRV_DIAGNOSTIC " << position.index << ' ' << message << '\n';
        });
        if (!tools.Assemble(source,&raw) || !tools.Validate(raw)) throw std::runtime_error("invalid control IR");
        if (!ShaderCompiler::SanitizeAndOptimizeBinary(raw,optimized,true,true,true) || !tools.Validate(optimized))
            throw std::runtime_error("invalid qualified control IR");
        tools.Disassemble(optimized,&text);
        std::ofstream(out/(String(name)+".spvasm")) << text;
        std::ofstream binary(out/(String(name)+".spv"),std::ios::binary);
        binary.write(reinterpret_cast<const char*>(optimized.data()),optimized.size()*sizeof(Uint32));
        std::cout << "CPU_IR " << name << " words=" << optimized.size()
                  << " containsCopyMemory=" << (text.find("OpCopyMemory")!=String::npos) << '\n';
        return optimized;
    }
}

int main(int argc,char** argv) {
    try {
        if (argc!=2 && argc!=3) throw std::runtime_error("explicit external output directory required");
        const Bool cacheControlOnly=argc==3 && String(argv[2])=="--cache-control-only";
        const std::filesystem::path rawOut(argv[1]);std::filesystem::create_directories(rawOut);
        Initialize();
        MG_Config::Features.AsyncShaderCompile=MG_Config::QuirkOverride::ForceOff;
        MG_Config::Features.ShaderTranslationCache=MG_Config::QuirkOverride::ForceOn;
        // 数组<struct<double>>整体常量写 storage 是真实副作用；不能只查标量/向量。
        const auto aggregate=Binary(GL_COMPUTE_SHADER,R"(#version 430 core
layout(local_size_x=1) in;
struct P { double d; };
layout(std430,binding=0) buffer O { P values[2]; };
void main(){ P a[2]=P[2](P(16777217.0lf),P(16777216.0lf)); values=a; }
)");
        Check("aggregate-storage-float64-required",ShaderCompiler::ModuleRequiresFloat64(aggregate));
        const auto arrayStore=QualifiedAssembly("array-struct-store",R"(
OpCapability Shader
OpCapability Float64
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %block Block
OpMemberDecorate %element 0 Offset 0
OpDecorate %array ArrayStride 8
OpMemberDecorate %block 0 Offset 0
OpDecorate %target DescriptorSet 0
OpDecorate %target Binding 0
%void = OpTypeVoid
%function = OpTypeFunction %void
%double = OpTypeFloat 64
%uint = OpTypeInt 32 0
%two = OpConstant %uint 2
%zero = OpConstant %uint 0
%element = OpTypeStruct %double
%array = OpTypeArray %element %two
%block = OpTypeStruct %array
%blockPointer = OpTypePointer StorageBuffer %block
%arrayPointer = OpTypePointer StorageBuffer %array
%target = OpVariable %blockPointer StorageBuffer
%first = OpConstant %double 16777217
%second = OpConstant %double 16777216
%firstElement = OpConstantComposite %element %first
%secondElement = OpConstantComposite %element %second
%value = OpConstantComposite %array %firstElement %secondElement
%main = OpFunction %void None %function
%entry = OpLabel
%address = OpAccessChain %arrayPointer %target %zero
OpStore %address %value
OpReturn
OpFunctionEnd
)",rawOut);
        Check("constant-array-struct-store-requires-float64",ShaderCompiler::ModuleRequiresFloat64(arrayStore));
        const auto pointerCopy=QualifiedAssembly("pointer-copy",R"(
OpCapability Shader
OpCapability Float64
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %block Block
OpMemberDecorate %block 0 Offset 0
OpDecorate %source DescriptorSet 0
OpDecorate %source Binding 0
OpDecorate %target DescriptorSet 0
OpDecorate %target Binding 1
%void = OpTypeVoid
%function = OpTypeFunction %void
%double = OpTypeFloat 64
%block = OpTypeStruct %double
%pointer = OpTypePointer StorageBuffer %block
%source = OpVariable %pointer StorageBuffer
%target = OpVariable %pointer StorageBuffer
%main = OpFunction %void None %function
%entry = OpLabel
OpCopyMemory %target %source
OpReturn
OpFunctionEnd
)",rawOut);
        Check("pointer-only-copy-requires-float64-storage",ShaderCompiler::ModuleRequiresFloat64(pointerCopy));
        const auto unused=Binary(GL_FRAGMENT_SHADER,R"(#version 400 core
uniform uint inputValue; double unused(){return double(inputValue);}
out vec4 selected; void main(){selected=vec4(0,1,0,1);}
)");
        Check("unused-function-no-float64-required",!ShaderCompiler::ModuleRequiresFloat64(unused));
        const auto supportedLayout=Binary(GL_FRAGMENT_SHADER,R"(#version 430 core
layout(std430,binding=0) buffer Values { double unused; uint marker; };
out vec4 selected;void main(){selected=marker==7341u?vec4(0,1,0,1):vec4(1,0,0,1);}
)");
        Check("std430-layout-only-no-wide-values",!ShaderCompiler::ModuleRequiresFloat64(supportedLayout,{},false) &&
              ShaderCompiler::ModuleRequiresFloat64(supportedLayout));
        Vector<Uint32> flattenedLayout;
        Check("actual-flatten-equivalent-layout-route",ShaderCompiler::FlattenFloat64StorageLayout(supportedLayout,flattenedLayout,true) &&
              !ShaderCompiler::ModuleRequiresFloat64(flattenedLayout));
        const auto unsupportedLayout=Binary(GL_FRAGMENT_SHADER,R"(#version 400 core
layout(std140) uniform Values { double unused; uint marker; };
out vec4 selected;void main(){selected=marker==7341u?vec4(0,1,0,1):vec4(1,0,0,1);}
)");
        Vector<Uint32> stillUniform;
        Check("ubo-layout-no-equivalent-route",!ShaderCompiler::ModuleRequiresFloat64(unsupportedLayout,{},false) &&
              ShaderCompiler::FlattenFloat64StorageLayout(unsupportedLayout,stillUniform,true) &&
              ShaderCompiler::ModuleRequiresFloat64(stillUniform));

        const String normal=R"(#version 400 core
uniform uint a;uniform uint b;out vec4 selected;
void main(){float d=float(a)-float(b);selected=vec4(d);}
)";
        const String precise=R"(#version 400 core
uniform uint a;uniform uint b;out vec4 selected;
void main(){double d=double(a)-double(b);selected=vec4(float(d));}
)";
        ClearProgramTranslationCache();
        const auto normalShader=Shader(normal,ShaderStage::Fragment,100);
        const auto cold=Program(normalShader,200);
        Check("normal-cold-link",cold->GetLinkStatus() && cold->GetSpirvStatus());
        const auto cacheKey=ExactKey(normalShader);
        const auto payload=GetProgramTranslationCache().Find(cacheKey);
        Check("actual-l1-payload-input-calibration",payload && payload->link.linkStatus && payload->spirv.spirvStatus);
        if (!payload) throw std::runtime_error("actual L1 key calibration failed");
        const auto before=GetProgramTranslationCache().Stats();
        const auto warm=Program(Shader(normal,ShaderStage::Fragment,101),201);
        const auto after=GetProgramTranslationCache().Stats();
        Check("real-l1-warm-hit",warm->GetLinkStatus() && after.hits>before.hits);

        // 旧 namespace 污染控制：完整 payload 来自上面真实合法 Float32 producer，
        // 代表旧 demote 成功条目可携带的32位执行数据；不是虚称从旧GPU库dump的payload。
        // 故意把该完整成功 payload放进同源FP64输入的旧layout5 key，测试新epoch能否
        // 阻断真实L1成功旁路。配套只将生产key epoch改回5的反控必须命中并失败。
        const auto preciseShader=Shader(precise,ShaderStage::Fragment,102);
        const auto currentKey=ExactKey(preciseShader);
        String legacyBlob=*currentKey.blob;const Uint32 legacyLayout=5;
        std::memcpy(legacyBlob.data()+sizeof(Uint32),&legacyLayout,sizeof(legacyLayout));
        const auto legacyKey=MakeTranslationCacheKey(Move(legacyBlob));
        ClearProgramTranslationCache();
        GetProgramTranslationCache().Insert(legacyKey,payload,ProgramTranslationResultBytes(*payload));
        Check("actual-legacy-entry-installed",GetProgramTranslationCache().Find(legacyKey)==payload);
        const auto prior=GetProgramTranslationCache().Stats();
        const auto rejected=Program(preciseShader,202);
        const auto later=GetProgramTranslationCache().Stats();
        std::cout << "CPU_CACHE legacyHitsBefore=" << prior.hits << " after=" << later.hits
                  << " link=" << rejected->GetLinkStatus() << " log=" << rejected->GetInfoLog() << '\n';
        Check("legacy-epoch-cannot-bypass-float64-link",!rejected->GetLinkStatus() &&
              rejected->GetInfoLog().find("Float64")!=String::npos && later.hits==prior.hits);
        const auto again=Program(Shader(precise,ShaderStage::Fragment,103),203);
        Check("repeated-float64-rejection",!again->GetLinkStatus() && again->GetInfoLog().find("Float64")!=String::npos);
        // 真实对象在 current 使用计数下保存有完整 B 输出的旧执行版本；最近失败仍失败。
        cold->AddCurrentBinding();cold->AttachShader(preciseShader);cold->Link(false);cold->JoinLinkAndSpirv();
        Check("current-snapshot-keeps-old-spv",!cold->GetLinkStatus() && cold->GetPreservedExecutable() &&
              cold->GetPreservedExecutable()->GetSpirvStatus() && cold->GetPreservedExecutable()->GetLinkStatus());
        cold->RemoveCurrentBinding();
        Check("last-current-retirement-releases-snapshot",!cold->GetPreservedExecutable());
        // 与现有AsyncSpirvPhaseTest相同真实单worker形状：先排多个compile/link，
        // 非current取消B仍不等待；current必须落实旧B与已接受写入后再开始新link。
        // epoch阴性只运行其明确污染的cache合同；异步窗口需独立正常production二进制
        // 实测，不把反控运行时调度差异无窗口当成缓存语义错误或放宽正例断言。
        if (!cacheControlOnly) {
        MG_Config::Features.AsyncShaderCompile=MG_Config::QuirkOverride::ForceOn;
        MG_Util::Async::ShaderCompilePool::Get().SetMaxConcurrency(1);
        Vector<SharedPtr<ShaderObject>> asyncShaders;Vector<SharedPtr<ProgramObject>> asyncPrograms;
        for (Uint i=0;i<24;++i) {
            const String text="#version 400 core\nuniform float uValue;out vec4 selected;void main(){float r=uValue+"+
                std::to_string(i)+".0;for(int j=0;j<200;++j){r=sin(r+float(j));}selected=vec4(r);}";
            auto shader=MakeShared<ShaderObject>(ShaderStage::Fragment,1000+i);shader->SetShaderSource(text);shader->Compile();
            auto program=MakeShared<ProgramObject>(2000+i);program->AttachShader(shader);program->Link(false);
            asyncShaders.push_back(shader);asyncPrograms.push_back(program);
        }
        Bool cancelled=false,buffered=false;
        for (SizeT i=0;i<asyncPrograms.size();++i) {
            const auto& object=asyncPrograms[i];if (!object->GetLinkStatus()) throw std::runtime_error(object->GetInfoLog());
            if (!cancelled && !object->IsSpirvComplete()) {
                object->CancelLink();cancelled=true;
                Check("noncurrent-cancel-keeps-old-phase-contract",object->GetLinkStatus() && !object->GetSpirvStatus());
                continue;
            }
            if (!buffered && object->IsSpirvPending()) {
                const Int location=object->GetUniformLocation("uValue");const float marker=7.0f;
                if (location<0 || !object->BufferUniformWrite(location,0,&marker,sizeof(marker)))
                    throw std::runtime_error("actual pending write did not establish fixture");
                object->AddCurrentBinding();asyncShaders[i]->SetShaderSource(precise);asyncShaders[i]->Compile();
                object->Link(false);object->JoinLinkAndSpirv();
                const auto& priorExecutable=object->GetPreservedExecutable();float actual=0;
                if (priorExecutable) {
                    const auto offset=priorExecutable->GetUniformOffset(location);
                    if (offset+sizeof(actual)<=priorExecutable->GetUBOSize())
                        std::memcpy(&actual,static_cast<const Uint8*>(priorExecutable->GetUBOData())+offset,sizeof(actual));
                }
                std::cout << "CPU_ASYNC pendingValue=" << actual << " currentLastLink=" << object->GetLinkStatus() << '\n';
                Check("current-pending-b-uniform-shadow-preserved",!object->GetLinkStatus() && priorExecutable &&
                      priorExecutable->GetSpirvStatus() && actual==marker);
                object->RemoveCurrentBinding();buffered=true;
            }
        }
        Check("actual-noncurrent-pending-window-observed",cancelled);
        Check("actual-current-pending-window-observed",buffered);
        }
        MG_Util::Async::ShaderCompilePool::Get().StopAndDrain();
        std::cout << "CPU_RESULT completed=1 failures=" << failures << '\n';
        return failures?1:0;
    } catch (const std::exception& error) {
        std::cerr << "CPU_RESULT completed=0 exception=" << error.what() << '\n';
        return 2;
    }
}
