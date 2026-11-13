# CPU mip 的按需零页存储

`MipmapByteStorage` 保留每个 mip 的独占完整地址范围及所有有效像素。
POSIX 平台上至少 1 MiB 的块用匿名读写映射，其他块用普通分配器。
纹理定义阶段预留容量；首次 `MapData` 扩大逻辑大小时，新匿名页已经为零，
不再通过 `memset` 触碰整张纹理。实际 patch、整图上传、CPU clear/copy 和读回仍正常消费所需字节。

这不是 GPU shadow 淘汰，也不按整合包名称启用特例。
收缩后重扩展的旧 high-water 区域会显式清零；扩容复制有效前缀；move 转移唯一所有权。
预留失败不破坏旧块，既有 `AllocateLevel` 分配边界保留，容量足够的 `MapData` 不增加分配点。
没有使用 `MAP_NORESERVE`，也没有给线程、全局 allocator 或 GPU 改内存预算。

逻辑大小、已初始化可读范围、映射容量与 RSS 是不同指标。
匿名零页只避免不必要的首次整图写入；完整消费者仍可能触碰全部页面，不能据此承诺整包不再 OOM。
旧的纯 `reserve` 128 MiB 真机实验本来就只增加约 68 KiB RSS，因此不能把所有大额 reserve 都叫实际驻留内存。

2026-11-13 14:31:10 +08:00 的 Linux 验证使用真实 `MipmapStorage`，
与精确 `7078c92b79f49d903a1bfadfde9be423fa58e653` 的头文件/实现作为负控：
128 MiB 首次 Map、跨页只读和小块写入，候选 RSS 增量 256 KiB，旧实现 131328 KiB 并触发预期断言。
候选还通过缩小/重扩展、真实扩容、有效像素保留、move、mip 截断、压缩原件边界及 ASan/UBSan。
证据：工作区 `run/mgl-pages-1113/host-r1/receipt.json`。OHOS/GPU/整包资格分别登记，不能由此宿主结果推定。
