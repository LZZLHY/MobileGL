// MobileGL - MobileGL/MG_State/GLState/BufferState/PipeResource.h
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once
#include <Includes.h>
#include <MG_Util/Types.h>
#include <bit>
#include <new>
#include <vector>

namespace MobileGL::MG_State::GLState {
    // GL_MIN_MAP_BUFFER_ALIGNMENT. GL 4.2 / ARB_map_buffer_alignment fix the minimum at 64 and
    // MobileGL advertises exactly that (MG_Impl/GLImpl/Getter/GL_Getter.cpp reads this constant),
    // so under-reporting is not available - the implementation has to be brought up to the number
    // instead. The promise is about POINTERS, not just the query: glMapBuffer must return a
    // 64-byte-aligned pointer, and glMapBufferRange must return one whose base - the returned
    // pointer minus the offset the caller asked for - is. Every pointer the frontend hands out
    // comes from the shadow below or from BufferObject's staging buffer, and std::vector only
    // promises alignof(std::max_align_t) (16 on aarch64), so both allocations carry the alignment
    // themselves. One constant for the getter and the allocator, because the two may never
    // disagree - the same reason the atomic-counter limits are shared through
    // MG_Util/ShaderTranspiler/Types.h.
    inline constexpr SizeT MIN_MAP_BUFFER_ALIGNMENT = 64;

    // Allocator that gives every allocation MIN_MAP_BUFFER_ALIGNMENT. Deliberately minimal: the
    // vectors it backs hold raw bytes and are only ever sized, so allocate/deallocate plus the
    // rebinding and equality boilerplate std::vector requires is the whole interface.
    template <typename T>
    struct MapAlignedAllocator {
        using value_type = T;

        MapAlignedAllocator() noexcept = default;
        template <typename U>
        MapAlignedAllocator(const MapAlignedAllocator<U>&) noexcept {}

        T* allocate(SizeT count) {
            if (count == 0) return nullptr;
            return static_cast<T*>(
                ::operator new(count * sizeof(T), std::align_val_t{MIN_MAP_BUFFER_ALIGNMENT}));
        }
        void deallocate(T* pointer, SizeT) noexcept {
            ::operator delete(pointer, std::align_val_t{MIN_MAP_BUFFER_ALIGNMENT});
        }

        template <typename U>
        Bool operator==(const MapAlignedAllocator<U>&) const noexcept {
            return true;
        }
        template <typename U>
        Bool operator!=(const MapAlignedAllocator<U>&) const noexcept {
            return false;
        }
    };

    // Byte store for anything the application may end up holding a mapped pointer into.
    using MapAlignedData = std::vector<Uint8, MapAlignedAllocator<Uint8>>;

    // Opaque, refcounted handle to the backend's GPU storage for one buffer
    // (the driver-side resource). The active backend derives from it and attaches
    // its own payload (VkBufferResource / GLESBufferResource). Held by PipeResource.
    class BackendBufferResource {
    public:
        virtual ~BackendBufferResource() = default;
    };

    // Mesa pipe_resource analogue for a GL buffer's storage. It owns the buffer's
    // bytes and its backend GPU resource, and abstracts WHERE the authoritative
    // bytes live so no caller has to branch on the mode:
    //
    //  - Shadow mode (default, non-persistent buffers): the bytes live in a CPU
    //    Vector (the shadow). GL writes mutate the shadow; the active backend keeps
    //    its own GPU copy in sync via BufferBackendOps (glBufferData/SubData/...).
    //
    //  - Persistent mode (coherent GL_MAP_PERSISTENT maps): the bytes live in the
    //    backend's host-visible, COHERENT, persistently-mapped GPU memory. That GPU
    //    buffer is the single source of truth - the app writes into it directly,
    //    every read/write resolves against it, and NO per-write backend transfer
    //    happens. The CPU shadow is released on adoption.
    //
    // Bytes() always returns a host-visible base pointer valid for [0, size) in both
    // modes, so readers/writers just call Bytes() (the size lives on the owning
    // BufferObject). (Named Bytes(), not Data(), to avoid colliding with the type
    // alias Data = Vector<Uint8> used for the shadow.)
    class PipeResource {
    public:
        Uint8* Bytes() { return m_gpuMapped != nullptr ? static_cast<Uint8*>(m_gpuMapped) : m_shadow->data(); }
        const Uint8* Bytes() const {
            return m_gpuMapped != nullptr ? static_cast<const Uint8*>(m_gpuMapped) : m_shadow->data();
        }

        // True once the buffer's bytes have been adopted into backend GPU memory.
        Bool IsGpuResident() const { return m_gpuMapped != nullptr; }

        // Shadow (re)allocation for non-persistent storage (glBufferData /
        // glBufferStorage before any persistent map). Mirrors the previous
        // power-of-two reserve + exact resize of the old m_dataPtr.
        void ResizeShadow(SizeT size) {
            m_shadow->reserve(std::bit_ceil(size == 0 ? SizeT{1} : size));
            m_shadow->resize(size);
        }
        // Direct shadow access, used only by the backend's upload-from-shadow path,
        // which never runs for a GPU-resident (persistent) buffer.
        MapAlignedData& Shadow() { return *m_shadow; }
        const MapAlignedData& Shadow() const { return *m_shadow; }

        // 将backend已用shadow初始化的coherent存储设为权威地址，并释放shadow。
        // 只能在映射地址发布前或没有活动映射时调用；BufferObject负责这项生命周期检查。
        // 应用的READ/WRITE/FLUSH_EXPLICIT标志不决定物理存储能否采用此模式。
        void AdoptPersistentMap(void* mappedBase) {
            m_gpuMapped = mappedBase;
            m_shadow->clear();
            m_shadow->shrink_to_fit();
        }

        // 仅在存储被合法重新定义时退回shadow，调用方随后重设其大小。普通unmap只终结
        // 应用映射，不要求释放backend存储；持久映射的指针同样在unmap之后不再对应用有效。
        // 不能把用途切换当成存储重新定义，immutable存储的非法重定义由前端拒绝。
        void ReleasePersistentMap() { m_gpuMapped = nullptr; }

        // Backend GPU resource, owned here in both modes.
        const SharedPtr<BackendBufferResource>& Backend() const { return m_backend; }
        void SetBackend(SharedPtr<BackendBufferResource> backend) { m_backend = std::move(backend); }
        SharedPtr<BackendBufferResource> ReleaseBackend() { return std::move(m_backend); }

    private:
        // MapAlignedData, not Data: a read-only glMapBuffer hands the application this very
        // pointer, and a range map hands it base + offset, so the base has to be on the
        // GL_MIN_MAP_BUFFER_ALIGNMENT grid for either to satisfy ARB_map_buffer_alignment.
        SharedPtr<MapAlignedData> m_shadow = MakeShared<MapAlignedData>();
        void* m_gpuMapped = nullptr;
        SharedPtr<BackendBufferResource> m_backend;
    };
} // namespace MobileGL::MG_State::GLState
