// MobileGL - MobileGL/MG_Backend/DirectVulkan/Renderer/SwapchainGeometry.h
// Copyright (c) 2025-2026 MobileGL-Dev
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace MobileGL::MG_Backend::DirectVulkan {
    struct SwapchainGeometry {
        VkExtent2D surfaceExtent;
        VkExtent2D imageExtent;
        VkSurfaceTransformFlagBitsKHR preTransform;
    };

    inline bool IsQuarterTurnSurfaceTransform(VkSurfaceTransformFlagBitsKHR transform) {
        return transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
               transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
    }

    // OHOS WSI 会反转缓冲区变换方向。只在支持反向档位时补偿，否则保留原有效变换。
    inline VkSurfaceTransformFlagBitsKHR ChooseOhosSwapchainPreTransform(
            VkSurfaceTransformFlagsKHR supportedTransforms, VkSurfaceTransformFlagBitsKHR proposed) {
        if (!IsQuarterTurnSurfaceTransform(proposed)) {
            return proposed;
        }
        const auto opposite = proposed == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR
            ? VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR
            : VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
        return (supportedTransforms & opposite) != 0 ? opposite : proposed;
    }

    inline SwapchainGeometry ChooseSwapchainGeometry(const VkSurfaceCapabilitiesKHR& caps,
                                                     VkExtent2D requested, bool preferIdentity) {
        const bool driverDefined =
            caps.currentExtent.width == UINT32_MAX || caps.currentExtent.height == UINT32_MAX;
        VkExtent2D surface = caps.currentExtent;
        // With pre-rotation the quarter-turn buffer is the transpose of the GL logical surface.
        // OHOS reports that allocated buffer back through currentExtent; accepting the echo would
        // transpose the logical size forever. A real resize changes the echo away from this pair.
        const bool transposedEcho = !preferIdentity && !driverDefined &&
            IsQuarterTurnSurfaceTransform(caps.currentTransform) &&
            surface.width == requested.height && surface.height == requested.width &&
            surface.width != surface.height;
        if (driverDefined || transposedEcho) {
            surface.width = std::clamp(requested.width, caps.minImageExtent.width, caps.maxImageExtent.width);
            surface.height = std::clamp(requested.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        const auto transform = preferIdentity && (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                                   ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
        auto image = surface;
        if (IsQuarterTurnSurfaceTransform(transform)) {
            std::swap(image.width, image.height);
        }
        return {surface, image, transform};
    }

    inline bool SwapchainSurfaceChanged(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D builtFrom,
                                       VkSurfaceTransformFlagBitsKHR observedTransform,
                                       VkSurfaceTransformFlagBitsKHR builtPreTransform) {
        if (caps.currentExtent.width == UINT32_MAX || caps.currentExtent.height == UINT32_MAX) return false;
        if (caps.currentTransform != observedTransform) return true;
        if (caps.currentExtent.width == builtFrom.width && caps.currentExtent.height == builtFrom.height) {
            return false;
        }
        // The quarter-turn buffer is reported in the transposed surface space. Treat that echo
        // as unchanged only while the pre-transform that created the buffer is still live.
        if (IsQuarterTurnSurfaceTransform(builtPreTransform) &&
            caps.currentExtent.width == builtFrom.height && caps.currentExtent.height == builtFrom.width) {
            return false;
        }
        return true;
    }
}
