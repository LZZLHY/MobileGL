# Running the OpenGL CTS against MobileGL

This directory contains three supported paths:

- Android arm64 / MobileGL EGL: the KHR-GL33 workflow documented below and in
  `skills/gl-cts-on-mobilegl/SKILL.md`.
- Windows x64 / MobileGL WGL: the GL30-GL46 pipeline in
  `scripts/wgl_glcts_pipeline.py`, documented by
  `skills/wgl-gl-cts-on-mobilegl/SKILL.md`.
- Desktop Linux x64 / MobileGL EGL: `scripts/run_cts_local.py` against the
  `mobilegl-desktop` VK-GL-CTS target, documented in "Desktop Linux workflow"
  below and in `skills/linux-gl-cts-on-mobilegl/SKILL.md`. This is the path that
  needs no device and no GPU.

Windows prerequisites are Git, Python 3.9+, CMake, Visual Studio 2022's Desktop
C++ workload, and a Vulkan SDK visible to CMake. DirectVulkan also needs a
working Vulkan loader plus a GPU-vendor ICD and driver; the SDK alone is not a
GPU driver.

For Windows, start with:

```powershell
python tools\cts\scripts\wgl_glcts_pipeline.py --help
```

The pipeline builds MobileGL as a drop-in `opengl32.dll`, builds or reuses
`glcts.exe`, checks that WGL loaded MobileGL rather than the system driver,
resumes individual suites after crashes/timeouts, and writes Markdown plus JSON
reports below the printed `runs/<first-16-of-run-fingerprint>` directory. Its
manifest records provenance and the runner settings used to validate a resume.

## Desktop Linux workflow

### Float64 限制与 current/最近链接合同

无 native Float64 且无等价表示路径时，合法 shader 的 parse 仍可成功；链接在
phase-A 成功发布前检查保宽、合法优化后的真实 IR，所需64位值或外部布局未解决
则 `LINK_STATUS=false`，program InfoLog 标明阶段与 Float64 要求。不能把 silent
demote 的成功或后期 no-op draw 当作可用执行。GL4.3/GLSL4.60 的历史声明保持原样，
这种明确拒绝只是错误输出限制，仍不满足所声明 core 版本的全部必需资格。

仅剩布局且没有wide值/copy需求的SSBO，使用既有flatten-only实际结果证明等价；
这个路径不会先用数值demote掩盖需求。活外UBO的未读double仍可能约束后续成员offset，
没有等价布局证明时明确拒绝。软件owned默认uniform由真实glslang `defaultBlock`
标记区分，不因名字含内部字样或GL表缺项就认定它是owned。未用且已消去的函数/
表达式继续允许；本轮已验证的native fragment数值/pack路径保留原宽度。既有
`ProgramSpirvTask` 的64位vertex-input→whole-program demote例外未在此候选修复，
本轮没有授予64位vertex-input资格，也没有覆盖所有native shader路径。

当前执行与最新链接尝试分别处理：current旧成功对象在新尝试开始前落实自身pending B
和已接受uniform写入，保存完整owned reflection/routing/UBO/sampler/block/image/TF
及版本；新失败query/named setter不借旧metadata通过，旧location setter保持原生
错误边界。仅draw/dispatch可继续消费旧执行快照；成功新link或最终解绑退休旧资源。
快照的backend lifetime独立，TF原program归属另按原owner lifetime检查，防名字复用。
非current仍用原异步cancel-not-join行为，不加入新全局manager。

以下独立冷进程入口直接核选定生产DSO，`--expected-library-sha` 必须由调用者对
实际文件独立取得；报告拒绝覆盖。原生参考先校准数值/像素，库输出不自制golden。

```sh
python tools/cts/probe/fp64_link_contract.py --mode mobilegl --library <actual-so> --expected-library-sha <host-sha> --fp64 reject --report <outside-checkout-nine.json>
python tools/cts/probe/fp64_layout_contract.py --mode mobilegl --library <actual-so> --expected-library-sha <host-sha> --fp64 reject --report <outside-checkout-layout.json>
python tools/cts/probe/fp64_tf_owner_contract.py --mode mobilegl --library <actual-so> --expected-library-sha <host-sha> --report <outside-checkout-tf.json>
python tools/cts/scripts/run_fp64_cpu_contract.py --source-dir <checkout> --build-dir <actual-linux-Ninja-cache> --output-dir <new-outside-checkout-dir>
```

本次已验证的DirectGLES用 `reject`，具备真实shaderFloat64的lavapipe DirectVulkan和
原生desktop参考用 `native`；不能按backend名字预设其它设备也支持。九项场景覆盖
live外部资源、重复失败/成功relink/退休、unused消去、动态uint精度与旧错误保留；
layout场景保offset8/16B与实际uint-only等价SSBO；TF场景实际读回三项7.0并区分原/
其它program；CPU入口只证明生产IR/cache/异步对象合同，不冒充GPU/设备资格。
current执行保护的实际像素范围为monolithic graphics；separable/pipeline失败重链接
与真实compute dispatch另需定向资格，不能从这些控制外推全部context执行路线。

CPU入口重用显式缓存的真实Ninja argv/对象，检查checkout归属，保留全部新输出；
旧epoch控制仅将生产key layout6退回5，实际L1注入的完整payload来自真实合法Float32
producer，表示旧namespace可携带的成功数据，不虚称从旧GPU库dump了payload。另
生成只恢复TF旧pointer比较的独立控制库，原候选不被覆盖。完整必需check/唯一完成
footer及精确阴性均检查，退出码本身不授予PASS；Python `-O` 不删除EGL实际调用。

客户端EGL扩展声明另用真实生产库检查：

```sh
python tools/cts/probe/egl_client_extension_contract.py --library <actual-libMobileGL.so> --output <outside-checkout-report.json> --expect-withdrawn
```

当前未提供完整`EGL_EXT_platform_base`三个EXT入口，因此不宣告该客户端扩展；
`EGL_KHR_platform_base`也没有官方Registry扩展身份。两个surface EXT名字仍不可调用，
这项撤除不代表window/pixmap功能实现。现有core EGL1.5入口与window/pbuffer保持原行为，
core pixmap仅状态句柄、配置PIXMAP位和legacy getPlatformDisplayEXT属性处理限制另行治理。
未来可用准确surface capability与合法拒绝/error语义完整提供三个命令，再独立资格后宣告。

The `mobilegl-desktop` target builds `glcts` as an ordinary host executable that
reaches OpenGL only through `libMobileGL.so`. Both backends run headless with no
GPU at all, which makes this the cheapest way to measure a single test group
while working on it.

Apt packages: `mesa-vulkan-drivers` (lavapipe, for DirectVulkan),
`libegl1-mesa-dev` and `libgles2-mesa-dev` (the system EGL/ES that DirectGLES
drives), `libvulkan-dev`, `ninja-build`.

```sh
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DMOBILEGL_BUILD_TEST=OFF -DMOBILEGL_BUILD_BENCHMARK=OFF
cmake --build build-linux --parallel "$(nproc)"

python tools/cts/scripts/sync_to_cts.py "$CTS"
git -C "$CTS" apply <path-to>/tools/cts/patches/0001-fbo-color-texture-attachment.patch
cmake -S "$CTS" -B "$CTS/build-cts" -G Ninja -DDEQP_TARGET=mobilegl-desktop \
      -DCMAKE_BUILD_TYPE=Release
ninja -C "$CTS/build-cts" glcts

python tools/cts/scripts/run_cts_local.py --backend DirectGLES \
    --glcts "$CTS/build-cts/external/openglcts/modules/glcts" \
    --lib build-linux/libMobileGL.so \
    --caselist cases.txt --outdir runs/gles --env EGL_PLATFORM=surfaceless
python tools/cts/scripts/qpa_report.py runs/gles --label DirectGLES
```

`EGL_PLATFORM=surfaceless` is not optional for DirectGLES: without a `/dev/dri`
node Mesa's EGL fails `eglInitialize` on the default display, and MobileGL
reports that as `EGL_BAD_ALLOC` out of `eglCreatePbufferSurface`. DirectVulkan
needs nothing extra - lavapipe exposes `VK_EXT_headless_surface`, which is what
the desktop platform port's pbuffer path requires.

Two known differences from a real GPU, both MobileGL's rather than the harness's:
DirectVulkan reads back zeros from the **default** framebuffer (a user FBO,
renderbuffer- or texture-attached, is correct on both backends), and lavapipe
supports renderbuffer formats that Adreno reports as unsupported, so the Android
runs see `NotSupported` where these do not.

### Reference results: KHR-GL45.direct_state_access

`opengl-cts-4.6.8.1`, the 371 `direct_state_access` cases of the `gl45-main`
mustpass list, lavapipe / Mesa 25.2.8, `--deqp-surface-type=fbo`.

| backend | conformance | strict Pass | Fail | InternalError | Crash |
| --- | ---: | ---: | ---: | ---: | ---: |
| DirectGLES | **82.48%** | 74.93% | 57 | 8 | 0 |
| DirectVulkan | **73.58%** | 73.32% | 87 | 8 | 3 |

`textures_storage_multisample_*` is most of what remains, and the two backends
are there for different reasons. Espryt passes the whole `2d` half and fails only
the `3d` one, which attaches a multisample **array** texture with
`glFramebufferTextureLayer` - layered attachments are not something the
framebuffer attachment model represents yet. Magma fails both halves: it cannot
sample a multisample texture at all, so the second pass reads back nothing.
`program_pipelines_*` needs ARB_separate_shader_objects, which is stubbed
throughout.

## Android KHR-GL33 workflow

Goal: measure how much of the OpenGL 3.3 core-profile conformance suite MobileGL
passes, separately for each backend (`DirectGLES`, `DirectVulkan`).

## How MobileGL is reached from a test binary

MobileGL ships its own EGL implementation alongside its desktop-GL implementation
in a single `libMobileGL.so`. A plain arm64 ELF in `/data/local/tmp` can therefore
drive it with no APK and no Activity:

1. `setenv("MOBILEGL_BACKEND_TYPE", "DirectGLES"|"DirectVulkan")` **before** the
   library is mapped — MobileGL parses its configuration from an ELF constructor.
2. `dlopen("libMobileGL.so")`, then `dlsym` the `egl*` and `gl*` entry points.
   MobileGL exports 45 EGL symbols and the desktop GL functions directly;
   `eglGetProcAddress` resolves the same set.
3. `eglBindAPI(EGL_OPENGL_API)`, choose a config with `EGL_RENDERABLE_TYPE =
   EGL_OPENGL_BIT`, then `eglCreateContext` with
   `EGL_CONTEXT_OPENGL_PROFILE_MASK = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT` and
   major/minor `3`/`3`.

This yields a genuine GL 3.3 core context (`GL_CONTEXT_PROFILE_MASK == 0x1`).

## Surface type, per backend

| backend | pbuffer (headless) | window |
|---|---|---|
| `DirectGLES` | works | works |
| `DirectVulkan` | **unusable** | works |

`DirectVulkan`'s pbuffer path builds a headless `VkSurfaceKHR` and so requires the
`VK_EXT_headless_surface` instance extension, which Adreno's Android driver does
not expose. It fails inside `eglMakeCurrent`, not at surface creation.

The workaround that keeps everything in a shell process: obtain a real
`ANativeWindow` from **`AImageReader`** (`AImageReader_newWithUsage` +
`AImageReader_getWindow`). It is an ordinary BufferQueue producer, so
`vkCreateAndroidSurfaceKHR` accepts it, and no Activity is involved. Register an
`onImageAvailable` listener that acquires and deletes each image — otherwise the
producer blocks once `maxImages` buffers are in flight and the next swap hangs.

## Why the suite must render into an FBO

On a window surface, `DirectVulkan`'s `glReadPixels` from the **default
framebuffer** returns all zeros, with no GL error, both before and after
`eglSwapBuffers`. `DirectGLES` on the identical window is correct, and readback
from a **user FBO is correct on both backends**.

Verified on two SoCs and two drivers, so this is MobileGL's behaviour rather than
a driver quirk:

| device | GPU | driver | default-FB | user FBO |
|---|---|---|---|---|
| Xiaomi 24129PN74C | Adreno 830 | Vulkan 1.3.284 / 512.800.46 | zeros | ok |
| Lenovo TB321FU | Adreno 750 | Vulkan 1.3.128 / 512.762.28 | zeros | ok |

dEQP verifies nearly every case through `glReadPixels`, so running it against the
default framebuffer would score `DirectVulkan` near zero for a reason unrelated to
conformance. The runs therefore use `--deqp-surface-type=fbo`, uniformly for both
backends so the two numbers stay comparable.

## Other constraints the harness must respect

- `eglMakeCurrent` requires **draw == read** and rejects `EGL_NO_SURFACE` with
  `EGL_BAD_MATCH`. dEQP's `surfaceless` platform is therefore unusable, which is
  why this port supplies its own `tcu::Platform`.
- MobileGL aborts during static teardown (`FORTIFY: pthread_mutex_lock called on a
  destroyed mutex`) *after* all work completes. Flush and `_exit()` so the exit
  code and the `.qpa` log survive.

## Contents

    probe/mgprobe.c        preflight gate: one backend x one surface type, checks
                           context version/profile and both readback paths
    scripts/qpa_report.py  .qpa -> pass rate, status histogram, worst groups

### Preflight

    aarch64-linux-android26-clang -O1 -o mgprobe mgprobe.c -ldl -llog -landroid -lmediandk
    adb push mgprobe libMobileGL.so /data/local/tmp/mgcts/
    adb shell 'cd /data/local/tmp/mgcts && LD_LIBRARY_PATH=. ./mgprobe \
        --backend DirectVulkan --surface imagereader --lib ./libMobileGL.so'

Exit status is 0 when a 3.3 core context came up and FBO readback is correct.
Default-framebuffer readback is reported but deliberately does not gate.
