// MobileGL - MobileGL/MG_Backend/DirectVulkan/Renderer/SwapchainGeometry.h
// Copyright (c) 2025-2026 MobileGL-Dev
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include <algorithm>
#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace MobileGL::MG_Backend::DirectVulkan {
    struct SwapchainGeometry {
        VkExtent2D surfaceExtent;
        VkExtent2D imageExtent;
        VkSurfaceTransformFlagBitsKHR preTransform;
    };

    inline SwapchainGeometry ChooseSwapchainGeometry(const VkSurfaceCapabilitiesKHR& caps,
                                                     VkExtent2D requested, bool preferIdentity) {
        VkExtent2D surface = caps.currentExtent;
        if (surface.width == UINT32_MAX || surface.height == UINT32_MAX) {
            surface.width = std::clamp(requested.width, caps.minImageExtent.width, caps.maxImageExtent.width);
            surface.height = std::clamp(requested.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        const auto transform = preferIdentity && (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                                   ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
        auto image = surface;
        if (transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
            transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
            std::swap(image.width, image.height);
        }
        return {surface, image, transform};
    }

    inline bool SwapchainSurfaceChanged(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D builtFrom,
                                       VkSurfaceTransformFlagBitsKHR observedTransform) {
        if (caps.currentExtent.width == UINT32_MAX || caps.currentExtent.height == UINT32_MAX) return false;
        return caps.currentExtent.width != builtFrom.width || caps.currentExtent.height != builtFrom.height ||
               caps.currentTransform != observedTransform;
    }
}
