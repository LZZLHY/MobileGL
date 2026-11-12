#include "../../MobileGL/MG_Backend/DirectVulkan/Renderer/TextureUploadRegion.h"
#include <iostream>
#include <stdexcept>
using MobileGL::MG_Backend::DirectVulkan::PackTextureUploadRegion;
static void require(bool value) { if (!value) throw std::runtime_error("texture upload region mismatch"); }
int main() {
    // 逐字节对照真实行/层跨度，覆盖非4字节RGB以及depth-stencil两种wire大小。
    std::size_t cases = 0;
    for (std::size_t bytes : {1, 2, 3, 4, 6, 8, 12, 16}) {
        std::vector<std::uint8_t> source(7 * 5 * 3 * bytes);
        for (std::size_t i = 0; i < source.size(); ++i) source[i] = static_cast<std::uint8_t>(i * 37 + 11);
        for (std::size_t z = 0; z < 3; ++z) for (std::size_t y = 0; y < 5; ++y) for (std::size_t x = 0; x < 7; ++x) {
            const std::array<std::size_t, 3> size{7 - x, 5 - y, 3 - z};
            std::vector<std::uint8_t> actual;
            require(PackTextureUploadRegion(source.data(), source.size(), {7,5,3}, {x,y,z}, size, bytes, actual));
            require(actual.size() == size[0] * size[1] * size[2] * bytes);
            std::size_t i = 0;
            for (std::size_t dz = z; dz < 3; ++dz) for (std::size_t dy = y; dy < 5; ++dy)
                for (std::size_t dx = x; dx < 7; ++dx) for (std::size_t c = 0; c < bytes; ++c)
                    require(actual[i++] == source[((dz * 5 + dy) * 7 + dx) * bytes + c]);
            ++cases;
        }
        // 拒绝无效源/范围/乘法溢出，并且保留调用方已有payload，不制造半写结果。
        std::vector<std::uint8_t> original{91,92,93}, actual = original;
        require(!PackTextureUploadRegion(source.data(), source.size()-1, {7,5,3}, {0,0,0}, {1,1,1}, bytes, actual));require(actual==original);
        require(!PackTextureUploadRegion(source.data(), source.size(), {7,5,3}, {7,0,0}, {1,1,1}, bytes, actual));require(actual==original);
        require(!PackTextureUploadRegion(source.data(), source.size(), {7,5,3}, {0,0,0}, {0,1,1}, bytes, actual));require(actual==original);
        require(!PackTextureUploadRegion(nullptr, source.size(), {7,5,3}, {0,0,0}, {1,1,1}, bytes, actual));require(actual==original);
        require(!PackTextureUploadRegion(source.data(), source.size(), {SIZE_MAX,5,3}, {0,0,0}, {1,1,1}, bytes, actual));require(actual==original);
        cases += 5;
    }
    std::cout << "texture region production helper: " << cases << " cases passed\n";
}
