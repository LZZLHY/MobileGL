#pragma once
#include "ProgramFactory.h"

namespace MobileGL::MG_Backend::DirectVulkan {
    // 位于正常uniform绑定公共入口，在dynamic-offset-only返回之前。记录的是绑定尝试，
    // 不将它称为已录制/已呈现draw；不为了观测重新解析descriptor或上传任何资源。
    void CaptureProgramBinding(const MG_State::GLState::ProgramObject& program,
                               const ProgramFactory::VkProgramObject& variant,
                               Uint32 frameSlot, VkPipelineBindPoint bindPoint) noexcept;
}
