/**
 * COPY buffer 绑定查询的生产 CPU 合同。
 *
 * 直接调用真实 GLImpl/GLState/BufferObject，不构造 getter 或复制绑定算法。名称预期
 * 来自本次 GenBuffers/BindBuffer 命令；16B 预期是固定独立输入。这里使用生产 CPU
 * 存储，因此不能授予 Vulkan/GLES GPU、OHOS 或完整 context 资格。
 * 每次查询检查单分量边界和真实 error；失败继续记录，末尾完整计数决定退出码。
 */
#include "Init.h"
#include "MG_Impl/GLImpl/Buffer/GL_Buffer.h"
#include "MG_Impl/GLImpl/Getter/GL_Getter.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <dlfcn.h>
#include <filesystem>
#include <stdexcept>

using namespace MobileGL;
#ifdef MGL_COPY_LIBRARY_BOOTSTRAP
/** 测试SO唯一额外出口只初始化原生产模型；不实现/转接被测getter或buffer函数。 */
extern "C" __attribute__((visibility("default"))) void amclCopyQueryContractInitialize() {
    MobileGL::Initialize();
}
#else
// 可执行文件不链接另一份GLImpl；每个函数来自本次明确dlopen的测试生产SO。
static decltype(&MG_Impl::GLImpl::GetIntegerv) GetIntegerv;
static decltype(&MG_Impl::GLImpl::GetInteger64v) GetInteger64v;
static decltype(&MG_Impl::GLImpl::GetFloatv) GetFloatv;
static decltype(&MG_Impl::GLImpl::GetDoublev) GetDoublev;
static decltype(&MG_Impl::GLImpl::GetBooleanv) GetBooleanv;
static decltype(&MG_Impl::GLImpl::GetError) GetError;
static decltype(&MG_Impl::GLImpl::GenBuffers) GenBuffers;
static decltype(&MG_Impl::GLImpl::BindBuffer) BindBuffer;
static decltype(&MG_Impl::GLImpl::DeleteBuffers) DeleteBuffers;
static decltype(&MG_Impl::GLImpl::BufferData) BufferData;
static decltype(&MG_Impl::GLImpl::CopyBufferSubData) CopyBufferSubData;
static decltype(&MG_Impl::GLImpl::MapBufferRange) MapBufferRange;
static decltype(&MG_Impl::GLImpl::UnmapBuffer) UnmapBuffer;
namespace {
    template<class Function> Function Resolve(void* library,const char* name,const char* expectedLibrary) {
        // dlsym会搜索依赖；逐地址dladdr+canonical路径必须落在本次测试SO，不能系统同名
        // 入口绕过被测实现。缺入口属于搭建失败，不能当作旧query被行为断言检出。
        void* address=dlsym(library,name);Dl_info identity{};
        if (!address || !dladdr(address,&identity) || !identity.dli_fname ||
            std::filesystem::canonical(identity.dli_fname)!=std::filesystem::canonical(expectedLibrary))
            throw std::runtime_error(std::string("actual selected SO does not own ")+name);
        std::cout << "COPY_FUNCTION name=" << name << " module=" << identity.dli_fname << '\n';
        return reinterpret_cast<Function>(address);
    }
    unsigned checks = 0, failures = 0;
    void Check(const std::string& name, bool passed) {
        // 不用assert，Release/NDEBUG也必须保留实际语义判据。
        ++checks; if (!passed) ++failures;
        std::cout << "COPY_CHECK " << name << " pass=" << passed << '\n';
    }
    void Drain() {
        // 只在独立case边界消费前一失败的错误，避免旧查询污染后续copy正控。
        for (unsigned i=0; i<16 && GetError()!=GL_NO_ERROR; ++i) {}
    }
    template<class T> struct Scalar {
        T before = static_cast<T>(17), value = static_cast<T>(73), after = static_cast<T>(29);
        bool Guard() const { return before==static_cast<T>(17) && after==static_cast<T>(29); }
    };
    template<class T, class Query>
    void QueryOne(const std::string& label, GLenum pname, GLuint expected, Query query) {
        Scalar<T> output; query(pname,&output.value);
        const GLenum error=GetError(), tail=GetError();
        const T expectedValue=static_cast<T>(expected);
        std::cout << "COPY_QUERY name=" << label << " pname=" << pname << " expected=" << expected
                  << " actual=" << static_cast<double>(output.value) << " error=" << error << " tail=" << tail << '\n';
        Check(label,output.Guard() && output.value==expectedValue && error==GL_NO_ERROR && tail==GL_NO_ERROR);
        Drain();
    }
    void QuerySet(const char* phase, GLuint read, GLuint write) {
        // 两个target独立检查，不能用一次末尾GetError掩盖哪一项失败。
        const GLenum pnames[]{GL_COPY_READ_BUFFER_BINDING,GL_COPY_WRITE_BUFFER_BINDING};
        const GLuint names[]{read,write};
        for (unsigned i=0;i<2;++i) {
            const std::string label=std::string(phase)+(i==0?"-read":"-write");
            QueryOne<GLint>(label+"-int",pnames[i],names[i],GetIntegerv);
            QueryOne<GLint64>(label+"-int64",pnames[i],names[i],GetInteger64v);
            QueryOne<GLfloat>(label+"-float",pnames[i],names[i],GetFloatv);
            QueryOne<GLdouble>(label+"-double",pnames[i],names[i],GetDoublev);
            QueryOne<GLboolean>(label+"-boolean",pnames[i],names[i]?GL_TRUE:GL_FALSE,GetBooleanv);
        }
    }
    void PendingError(GLenum pname, GLuint name) {
        // 先校准公开生产调用确实产生INVALID_VALUE，再重复注入。合法getter不得清掉
        // 原错误或添加INVALID_ENUM，也不得修改binding；不直接篡改错误账本。
        BufferData(GL_COPY_READ_BUFFER,-1,nullptr,GL_STATIC_DRAW);
        Check("pending-control",GetError()==GL_INVALID_VALUE && GetError()==GL_NO_ERROR);
        BufferData(GL_COPY_READ_BUFFER,-1,nullptr,GL_STATIC_DRAW);
        Scalar<GLint> output; GetIntegerv(pname,&output.value);
        const GLenum first=GetError(), second=GetError();
        Check(pname==GL_COPY_READ_BUFFER_BINDING?"pending-read":"pending-write",
              output.Guard() && output.value==static_cast<GLint>(name) && first==GL_INVALID_VALUE && second==GL_NO_ERROR);
        Drain();
    }
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    void* library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!library){std::cerr << dlerror() << '\n';return 2;}
    try {
        auto init=Resolve<void(*)()>(library,"amclCopyQueryContractInitialize",argv[1]);
#define COPY_BIND(name) name=Resolve<decltype(name)>(library,"gl" #name,argv[1])
        COPY_BIND(GetIntegerv);COPY_BIND(GetInteger64v);COPY_BIND(GetFloatv);COPY_BIND(GetDoublev);
        COPY_BIND(GetBooleanv);COPY_BIND(GetError);COPY_BIND(GenBuffers);COPY_BIND(BindBuffer);
        COPY_BIND(DeleteBuffers);COPY_BIND(BufferData);COPY_BIND(CopyBufferSubData);
        COPY_BIND(MapBufferRange);COPY_BIND(UnmapBuffer);
#undef COPY_BIND
        init();
    } catch(const std::exception& error){std::cerr << error.what() << '\n';return 2;}
    Drain();
    // 本独立进程初始copy slots应为空；不借被测查询值计算后续期望。
    QuerySet("default",0,0);
    GLuint names[3]{}; GenBuffers(3,names);
    Check("names",names[0] && names[1] && names[2] && names[0]!=names[1] && names[1]!=names[2]);
    BindBuffer(GL_COPY_READ_BUFFER,names[0]); BindBuffer(GL_COPY_WRITE_BUFFER,names[1]);
    Check("bind-two",GetError()==GL_NO_ERROR);
    QuerySet("distinct",names[0],names[1]);
    PendingError(GL_COPY_READ_BUFFER_BINDING,names[0]); PendingError(GL_COPY_WRITE_BUFFER_BINDING,names[1]);
    BindBuffer(GL_COPY_READ_BUFFER,names[2]); QuerySet("switch",names[2],names[1]);
    DeleteBuffers(1,&names[2]); QuerySet("delete-unbind",0,names[1]);
    BindBuffer(GL_COPY_READ_BUFFER,names[0]);
    constexpr std::array<uint32_t,4> expected{0x31415926u,0x27182818u,0x10203040u,0x55667788u};
    BufferData(GL_COPY_READ_BUFFER,sizeof(expected),expected.data(),GL_STATIC_DRAW);
    BufferData(GL_COPY_WRITE_BUFFER,sizeof(expected),nullptr,GL_STATIC_DRAW);
    CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,sizeof(expected));
    Check("copy-call",GetError()==GL_NO_ERROR);
    void* bytes=MapBufferRange(GL_COPY_WRITE_BUFFER,0,sizeof(expected),GL_MAP_READ_BIT);
    Check("copy-read-16bytes",bytes && std::memcmp(bytes,expected.data(),sizeof(expected))==0);
    const bool unmapped=bytes && UnmapBuffer(GL_COPY_WRITE_BUFFER)!=GL_FALSE;
    Check("copy-unmap",unmapped && GetError()==GL_NO_ERROR);
    QuerySet("after-copy",names[0],names[1]);
    // 还原本新进程的原空绑定再删除本测试名字；不得以新建context作为还原。
    BindBuffer(GL_COPY_READ_BUFFER,0); BindBuffer(GL_COPY_WRITE_BUFFER,0);
    DeleteBuffers(2,names); QuerySet("restored",0,0);
    Check("cleanup-error",GetError()==GL_NO_ERROR);
    std::cout << "COPY_RESULT completed=1 checks=" << checks << " failures=" << failures << '\n';
    return failures?1:0;
}
#endif
