#pragma once
#include <cstdint>

namespace MobileGL::MG_Backend {
/** 后端一次呈现的结果。presented 与后续准备状态独立：本帧已经呈现后，下一帧的
 * acquire仍可能失败。Deferred表示合法暂缓，不能推进宿主真实呈现计数；所有资源
 * 继续由原renderer持有，此结构不执行恢复或释放，也不跨公开DSO ABI传递。
 */
enum class PresentationStatus : std::uint32_t { Presented, Deferred, SurfaceLost, DeviceLost, Failed };
struct PresentationResult {
    PresentationStatus status = PresentationStatus::Failed;
    bool presented = false;
    std::int32_t nativeError = 0;
    explicit operator bool() const {
        return status == PresentationStatus::Presented || status == PresentationStatus::Deferred;
    }
};
}
