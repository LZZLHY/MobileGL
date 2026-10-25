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
    // 本次成功呈现所使用且返回时仍有效的窗口 drawable 尺寸，单位为应用表面像素。
    // Vulkan 的预旋转 imageExtent 可能交换宽高，这里使用默认 FBO 所在的 surfaceExtent。
    // 未呈现、失败或呈现后已退休该交换链时均为零；不能填请求尺寸或下一帧新资源尺寸。
    // 仅在 provider 内部传递，不扩展公开 C ABI，也不改变 presented 的历史呈现计数语义。
    std::uint32_t drawableWidth = 0;
    std::uint32_t drawableHeight = 0;
    explicit operator bool() const {
        return status == PresentationStatus::Presented || status == PresentationStatus::Deferred;
    }
};
}
