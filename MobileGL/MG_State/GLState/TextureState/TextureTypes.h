// MobileGL - MobileGL/MG_State/GLState/TextureState/TextureTypes.h
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once
#include "MG_Util/Types.h"
#include "MG_Util/Math/VectorTypes.h"

namespace MobileGL {
    namespace MG_State {
        namespace GLState {
            struct MipmapInput {
                IntVec3 texelSize = {0, 0, 0};
                SizeT byteSize = 0;
                // 只用于API允许初始内容未指定的定义。定义时仍reserve容量，保留原分配失败点；
                // 首次Map/CPU写入才零初始化，避免GPU-only纹理先触碰整层CPU页面。
                // 默认false保证已有实数据、压缩输入和其他调用方继续原行为。
                Bool deferCpuInitialization = false;
            };
        } // namespace GLState
    } // namespace MG_State
} // namespace MobileGL
