#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace MobileGL::MG_Backend::DirectVulkan {
    // 从完整CPU行/层提取真实写入块，再交格式转换。目的偏移仍由VkBufferImageCopy保留；
    // 此函数绝不把包围块外的旧shadow像素混入上传。范围/溢出失败时保持out原字节。
    inline bool PackTextureUploadRegion(const void* source, std::size_t sourceBytes,
                                         std::array<std::size_t, 3> full,
                                         std::array<std::size_t, 3> offset,
                                         std::array<std::size_t, 3> size,
                                         std::size_t texelBytes, std::vector<std::uint8_t>& out) {
        if (!source || texelBytes == 0) return false;
        std::size_t fullBytes = texelBytes, packedBytes = texelBytes;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (full[axis] == 0 || size[axis] == 0 || offset[axis] > full[axis] ||
                size[axis] > full[axis] - offset[axis]) return false;
            if (full[axis] > std::numeric_limits<std::size_t>::max() / fullBytes ||
                size[axis] > std::numeric_limits<std::size_t>::max() / packedBytes) return false;
            fullBytes *= full[axis]; packedBytes *= size[axis];
        }
        if (sourceBytes < fullBytes) return false;
        out.resize(packedBytes);
        const auto* input = static_cast<const std::uint8_t*>(source);
        const std::size_t rowBytes = size[0] * texelBytes;
        for (std::size_t z = 0; z < size[2]; ++z) {
            for (std::size_t y = 0; y < size[1]; ++y) {
                const std::size_t src = (((offset[2] + z) * full[1] + offset[1] + y) * full[0] + offset[0]) * texelBytes;
                const std::size_t dst = (z * size[1] + y) * rowBytes;
                std::memcpy(out.data() + dst, input + src, rowBytes);
            }
        }
        return true;
    }
}
