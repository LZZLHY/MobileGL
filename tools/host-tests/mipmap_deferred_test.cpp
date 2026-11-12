// 验证真实MipmapStorage的逻辑尺寸、延迟触页、CPU写入和移动生命周期。
// 这是CPU层验收，不代替真实GL前端/PBO及GPU clear/subimage/readback像素测试。
#include "MG_State/GLState/TextureState/MipmapStorage.h"
#include <new>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

static bool rejectAllocations = false;
void* operator new(std::size_t size) {
    if (rejectAllocations) throw std::bad_alloc();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace MobileGL;
using namespace MobileGL::MG_State::GLState;
// 直接观察真实容器，旧实现负控不用凭新getter返回值证明自身。
struct Storage : MipmapStorage {
    SizeT initialized(Uint level) const { return m_data.at(level).size(); }
    SizeT capacity(Uint level) const { return m_data.at(level).capacity(); }
};
static void require(bool value, const char* reason) {
    if (!value) { std::fprintf(stderr, "FAIL %s\n", reason); std::exit(1); }
}
static long residentKiB() {
    std::ifstream file("/proc/self/statm"); long pages = 0, resident = 0;
    file >> pages >> resident; require(file.good() || file.eof(), "proc statm readable");
    return resident * sysconf(_SC_PAGESIZE) / 1024;
}
int main() {
    static_assert(!std::is_copy_constructible_v<MipmapStorage>);
    static_assert(std::is_nothrow_move_constructible_v<MipmapStorage>);
    constexpr SizeT bytes = 128 * 1024 * 1024;
    Storage source;
    const long before = residentKiB();
    source.AllocateLevel(0, {{8192, 4096, 1}, bytes, true});
    const long defined = residentKiB();
    std::printf("null-definition logical=%zu initialized=%zu reserved=%zu rss_delta_kib=%ld\n",
        source.GetByteSize(0), source.initialized(0), source.capacity(0), defined - before);
    require(source.GetByteSize(0) == bytes && source.GetTexelSize(0) == IntVec3{8192, 4096, 1}, "defined metadata");
    require(source.initialized(0) == 0 && source.capacity(0) >= bytes, "reserve without initializing CPU texels");
    require(defined - before < 32 * 1024, "fresh large NULL definition does not touch every CPU page on Linux");
    require(!source.IsDirty(0), "undefined contents do not become a zero upload");

    // 移动必须保留预留空间；首次Map与后续全图写不申请新vector块。
    Storage moved = std::move(source);
    rejectAllocations = true;
    auto* data = static_cast<Uint8*>(moved.MapData(0));
    require(data != nullptr && moved.initialized(0) == bytes, "first Map materializes within reserved capacity");
    for (SizeT i = 0; i < bytes; i += 4096) require(data[i] == 0, "CPU first access chooses zero for undefined bytes");
    Uint8 first[] = {7, 19, 31, 43};
    moved.UpdateSubData(0, {first, sizeof(first)});
    rejectAllocations = false;
    require(std::memcmp(data, first, sizeof(first)) == 0 && moved.IsDirty(0), "real CPU update and dirty state");
    require(moved.GetDirtyRegion(0).CoversWholeLevel({8192, 4096, 1}), "full update dirty bounds");
    moved.MarkDirty(0, false);
    moved.AllocateLevel(0, {{8192, 4096, 1}, bytes, true});
    require(moved.initialized(0) == bytes && std::memcmp(moved.MapData(0), first, sizeof(first)) == 0,
        "already initialized shadow is not discarded by the new option");

    moved.AllocateLevel(2, {{4, 4, 1}, 64, true});
    require(moved.GetLevelCount() == 3 && moved.GetByteSize(1) == 0 && moved.GetByteSize(2) == 64, "sparse level metadata");
    rejectAllocations = true;
    require(moved.MapData(2) != nullptr, "small deferred level Map without allocation");
    rejectAllocations = false;
    moved.TruncateToLevelCount(1);
    require(moved.GetLevelCount() == 1 && moved.GetByteSize(2) == 0, "truncate logical and actual levels together");
    moved.AllocateLevel(1, {{2, 2, 1}, 16});
    require(moved.initialized(1) == 16, "default callers keep eager initialization");
    moved.SetCompressedImage(1, GL_COMPRESSED_RGBA, first, sizeof(first));
    require(moved.GetCompressedByteSize(1) == 4 && std::memcmp(moved.MapCompressedData(1), first, 4) == 0, "compressed original unchanged");
    std::puts("PASS real MipmapStorage metadata, memory, update, move, truncate and compressed-data boundaries");
}
