// MobileGL - demand-zero CPU texture storage
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#if defined(__linux__) || defined(__APPLE__) || defined(__OHOS__)
#include <sys/mman.h>
#endif

#include "MG_Util/Types.h"

namespace MobileGL::MG_State::GLState {
    /**
     * CPU mip 的独占字节存储，保留 vector 使用到的 size/capacity/resize/reserve 语义。
     * 大块 POSIX 存储在定义时用匿名映射预留；新页由内核提供零值，第一次 MapData
     * 不必写遍整张纹理。有效像素始终保留，不依赖 GPU 读回、不驱逐已上传 shadow。
     * 小块和非 POSIX 平台使用普通分配器，保持原来的显式清零行为。
     */
    class MipmapByteStorage {
    public:
        static constexpr SizeT kPageStorageThreshold = 1024 * 1024;

        MipmapByteStorage() = default;
        MipmapByteStorage(const MipmapByteStorage&) = delete;
        MipmapByteStorage& operator=(const MipmapByteStorage&) = delete;

        /** 外层 mip vector 搬移时转移唯一所有权，旧对象不再释放映射或分配器块。 */
        MipmapByteStorage(MipmapByteStorage&& other) noexcept { Swap(other); }
        MipmapByteStorage& operator=(MipmapByteStorage&& other) noexcept {
            if (this != &other) { Release(); Swap(other); }
            return *this;
        }
        ~MipmapByteStorage() { Release(); }

        Uint8* data() noexcept { return m_data; }
        const Uint8* data() const noexcept { return m_data; }
        SizeT size() const noexcept { return m_size; }
        SizeT capacity() const noexcept { return m_capacity; }
        bool empty() const noexcept { return m_size == 0; }

        /**
         * 预留发生在 AllocateLevel，失败保持旧块与有效像素不变并保留原有 bad_alloc 边界。
         * mmap 的长度不要求页对齐，内核按页处理；记录原始长度供匹配的 munmap 使用。
         */
        void reserve(SizeT requested) {
            if (requested <= m_capacity) return;
            if (requested > static_cast<SizeT>(std::numeric_limits<std::ptrdiff_t>::max()))
                throw std::bad_alloc();
            Uint8* replacement = nullptr;
            bool pages = false;
#if defined(__linux__) || defined(__APPLE__) || defined(__OHOS__)
            if (requested >= kPageStorageThreshold) {
                void* mapped = mmap(nullptr, requested, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (mapped == MAP_FAILED) throw std::bad_alloc();
                replacement = static_cast<Uint8*>(mapped);
                pages = true;
            }
#endif
            if (!pages) replacement = std::allocator<Uint8>{}.allocate(requested);
            const SizeT retainedSize = m_size;
            if (retainedSize != 0) std::memcpy(replacement, m_data, retainedSize);
            Release();
            m_data = replacement;
            m_capacity = requested;
            m_size = retainedSize;
            m_highWater = retainedSize;
            m_pages = pages;
        }

        /**
         * 首次扩展到未曾暴露给调用者的匿名页时，零值已经成立，省去整图 memset。
         * 缩小后重新扩展的旧 high-water 区域可能被调用者写过，必须按 vector 语义
         * 重新填充；非零填充值与普通分配器路径也始终显式填充。未保留足够容量时
         * 才调用 reserve，MipmapStorage 的定义期预留保证通常 MapData 不发生新分配。
         */
        void resize(SizeT requested, Uint8 value = 0) {
            reserve(requested);
            if (requested > m_size) {
                const SizeT fillEnd = (m_pages && value == 0)
                    ? std::min(requested, m_highWater) : requested;
                if (fillEnd > m_size) std::memset(m_data + m_size, value, fillEnd - m_size);
                m_highWater = std::max(m_highWater, requested);
            }
            m_size = requested;
        }

        /** 只暴露存储方式供定向验收，不将容量或“初始化字节”当作真实 RSS。 */
        bool UsesDemandZeroPages() const noexcept { return m_pages; }

    private:
        /** 析构及替换只释放自身块；不触碰共享 allocator 全局状态或其他纹理。 */
        void Release() noexcept {
            if (m_data != nullptr) {
#if defined(__linux__) || defined(__APPLE__) || defined(__OHOS__)
                if (m_pages) munmap(m_data, m_capacity);
                else
#endif
                    std::allocator<Uint8>{}.deallocate(m_data, m_capacity);
            }
            m_data = nullptr;
            m_size = m_capacity = m_highWater = 0;
            m_pages = false;
        }

        /** 所有字段作为一组搬移，避免 allocator 与 munmap 释放方式错配。 */
        void Swap(MipmapByteStorage& other) noexcept {
            std::swap(m_data, other.m_data);
            std::swap(m_size, other.m_size);
            std::swap(m_capacity, other.m_capacity);
            std::swap(m_highWater, other.m_highWater);
            std::swap(m_pages, other.m_pages);
        }

        Uint8* m_data = nullptr;
        SizeT m_size = 0;
        SizeT m_capacity = 0;
        SizeT m_highWater = 0;
        bool m_pages = false;
    };
}
