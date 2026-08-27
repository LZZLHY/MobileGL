# tl::expected (vendored)

Single-header fallback for toolchains at C++23 language level whose standard
library does not ship `std::expected` (libc++ 15 has no `<expected>` header at
all - e.g. the OpenHarmony SDK toolchain). Consumed only by
`MobileGL/MG_Util/ShaderTranspiler/Types.h`, and only when
`__cpp_lib_expected` is absent.

| | |
|---|---|
| Upstream | https://github.com/TartanLlama/expected |
| Tag | `v1.3.1` |
| File | `include/tl/expected.hpp` (byte-identical to upstream) |
| SHA-256 | `0788f7d544a2fdec1dd7a794e1e9863ad4cc6a8817982b6fd93fd98724b9fa19` |
| License | CC0 1.0 (public domain dedication) - see `COPYING` (SHA-256 `a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499`) |

Do not edit `expected.hpp` locally; replace it wholesale from a newer upstream
tag and update the hashes above.
