# Utility 插件

`ark_utility_plugin` 提供 GUID、定时器、调度和压缩模块。使用方链接 `ark::utility` 获取公开头文件和编译要求，再通过应用清单选择 `ark_utility_plugin` 加载实现。仅链接 SDK 目标不会加载模块。

## 公开接口

| 头文件 | 接口与行为 |
| --- | --- |
| `<ark/utility/guid_module.hpp>` | `guid_module::create_guid()` 和 `parse_uid()`；保留现有 GUID 模式及由总线地址生成的 worker 位 |
| `<ark/utility/timer_module.hpp>` | 由管理器时间戳驱动的一次或重复定时器；Utility 中只有该模块注册所属线程更新 |
| `<ark/utility/schedule_module.hpp>` | 间隔、指定时间和 cron 回调；保留现有调度工作线程配置 |
| `<ark/utility/compress_module.hpp>` | zlib 与 Zstandard 的 `compress()` / `decompress()`，支持二进制负载 |

插件注册完成后，在模块初始化中获取接口：

```cpp
#include <ark/utility/guid_module.hpp>
#include <ark/utility/timer_module.hpp>

auto* guids = manager.find_module<ark::guid_module>();
auto* timers = manager.find_module<ark::timer_module>();
const auto entity = guids->create_guid();
```

接口指针借用于已加载插件，使用期间必须保持插件存活。定时器成员回调还借用接收者，接收者应保留到回调完成或被移除。在所属线程以毫秒时间戳推进管理器；创建定时器本身不会推进时间。schedule 模块继续使用已有后台调度器，线程池配置保持为 16。本次迁移不增加回调取消、热更新或新的停机保证。卸载前，按[模块生命周期](../guides/modules_CN.md)完成生产者和回调的收尾。

插件按 GUID、timer、schedule、compress 顺序安装模块，卸载顺序相反。timer 的 `pre_shut()` 仍关闭时间轮。GUID 布局、压缩级别与算法、scheduler 行为不变。动态库主体名称仍为 `ark_utility_plugin`，保留平台扩展名和 Debug `_d` 后缀。

## 压缩缓冲区与并发

核心辅助头 `<ark/compress/zlib.hpp>`、`<ark/compress/zstd.hpp>` 和
`<ark/compress/lz4.hpp>` 为每次调用分配独立的堆缓冲区。成功时向输出字符串追加结果；
拒绝输入时保留输出字符串。调用方须提供可读的输入范围，并同步对共享输出字符串的访问。
压缩保留既有限制：算法计算的最坏输出大小必须严格小于 1 MiB。解压输出最大为
1 MiB，包含恰好 1 MiB 的情况。Zstandard 要求帧声明已知内容大小；大小超限、未知或
无效的帧在解码前被拒绝。无效压缩数据返回 `false`。LZ4 对空块解压仍返回 `false`。

zlib、LZ4 和 Zstandard 普通调用不再共享可写输出缓冲区。Zstandard 字典调用仍会修改
实例缓存的上下文；每个线程须使用独立的 `zstd_compress` 实例，或由调用方串行执行
字典初始化、字典调用和析构。字典初始化、所有权和复制行为保留既有约定。
本次修改不建立同一实例的字典并发保证，也不建立插件卸载安全保证。

启用 `ARK_BUILD_COMPRESSION_TESTS=ON` 和 vcpkg 的 `compression-tests` feature 后，
执行 `cmake --build <build-dir> --target check-compression`。启用时，`check-core` 也包含
此目标；Applications CI 会显式开启。检查覆盖映射后的公开头、容量边界、损坏帧、追加
语义，以及使用独立字典实例的并行往返。LZ4 仍通过核心辅助头直接使用；Utility 模块
继续只选择 zlib 或 Zstandard。

## 源码与构建树 SDK 迁移

公开头文件的唯一源码现在位于 `src/plugins/utility/`。CMake 将显式列出的七个公开头复制到构建树的 `src/plugins/sdk-include/ark/utility/`：四个模块接口，以及 `scheduler.hpp`、`ctpl.h`、`ctpl_stl.h`。通过 `ark::utility` 消费，保留现有 `<ark/utility/...>` 公开接口与辅助头路径，关闭运行时插件时同样有效。不要把私有插件目录加入使用方的包含路径。上游线程池头文件保留原字节和格式规则。安装后的消费方式见 [CMake 包安装说明](../get-started/installation_CN.md#安装并消费-cmake-包)：使用 `find_package(ark CONFIG REQUIRED COMPONENTS utility)` 和相同的 `ark::utility` 目标。安装头保留 `<ark/utility/...>` 路径；运行时插件仍需在应用清单中选择。

**源码不兼容变更：** SDK 移除 `ark/utility/{guid,timer,schedule,compress}_module_impl.hpp` 和 `ark/utility/utility_plugin.hpp`。实现类定义在各自 cpp 文件中，插件类型及返回接口的工厂属于私有装配。将直接构造实现类的代码改为公开接口查找，或应用自行维护实现。仅手写 `-I <ark-source>/include` 的调用需改用目标传递的包含目录。插件和使用方应一起重新编译；公开虚函数及动态入口签名保持不变，不代表兼容旧实现类二进制。

## Core 字符串匹配

`ark::core` 通过 `<ark/string_utils.hpp>` 提供 `string_utils::split()` 和 `replace()`。字符串分隔符为空时，将非空输入原样追加为一个元素；空输入不追加元素。替换模式为空时，原样返回输入，包括 `std::string` 中的空字符。无论操作次数是否设上限，都遵循此规则，避免空模式导致无限循环。

非空模式保持现有行为：拆分结果追加到输出容器，保留开头及中间的空元素，忽略末尾空元素，到达拆分上限后保留剩余后缀。上限按输出容器的总元素数比较；若只需统计本次结果，请使用空容器。替换按从左到右、不重叠的顺序执行，次数为 `0` 表示不限。C 字符串参数必须有效并以空字符结尾；字符串分隔符重载仍沿用 C 字符串匹配语义，此项修改不增加通用二进制子串匹配能力。


## Core UID 位分配

`<ark/uid_generator.hpp>` 保留默认的有符号 64 位 UID 布局：预留 1 位符号位、29 位时间戳、24 位工作节点和 10 位序号。合法字段生成的 ID 保持不变。`bits_allocator` 构造与 `alloc()` 支持 C++20 常量求值。

各字段位宽必须非负，合计为 63 位有效载荷；非法布局在执行移位前抛出 `std::invalid_argument`，Debug 与 Release 行为一致。分配的字段值必须非负且不超过各自位宽，否则 `alloc()` 抛出 `std::out_of_range`，避免工作节点或序号占用相邻字段。零位宽字段仅接受零值。运行时生成仍使用既有纪元、时钟检查和序号回绕规则；此项修改不改变 UID 格式，也不提供跨进程工作节点 ID 协调。

## Core 随机数工具

`<ark/random/random_generator.hpp>` 通过 `ark::core` 提供 `ark::random_generator`，无需加载 Utility 插件。有符号 `random(int, int)` 重载接受任意两个 `int` 端点，包括 `INT_MIN` 和 `INT_MAX`，范围计算不会溢出。端点不同时，先按大小排序，再返回 `[lower, upper)` 内的值。端点相同时，直接返回该值，不推进生成器状态。

范围选择保留现有的取模映射、种子序列和生成次数。端点不同的每次调用消耗一个随机数；使用相同种子并交换端点，得到相同序列。无符号重载的行为保持不变。

## Core Murmur 哈希

`<ark/hash/murmur_hash.hpp>` 通过 `ark::core` 提供 MurmurHash2，以及 MurmurHash3
的 x86-32、x86-128 和 x64-128 变体。调用方须提供非负 `int` 长度、可读输入缓冲区
（空输入也需要有效缓冲区），以及可写输出存储：32 位变体需要 4 字节，128 位变体需要
16 字节。输入和输出允许任意字节对齐，字读取和写入使用 `std::memcpy`。
MurmurHash3 使用 C++20 `std::rotl`，旋转次数保持不变。种子、尾部混合、结果字顺序和
原有公开签名不变；`murmur_hash3_x86_128` 仍为实例方法，其余变体仍为静态方法。
MurmurHash2 保留固定种子零。

Murmur 头文件自身仅使用标准 C++20 头文件，不再包含 `ark/macros.hpp` 或
`ark/platform.hpp`。调用方若使用其中的声明或宏，须显式包含对应头文件；公开宏定义
保持不变。头文件独立性不改变 `ark::core` 或其 CMake 包的依赖要求。
`check-murmur-hash-header` 仅通过隔离的包含目录编译逻辑公开头文件，不依赖 SDK
目标，并纳入 `check-core`。独立检查命令见[核心回归](../../tests/core/README_CN.md)。

### 摘要兼容性与旧结果入口

**不兼容变更：** 默认 MurmurHash3 现在在所有支持的平台上按小端方式读取完整块，
并输出规范的小端摘要字节。这保留原有小端 GCC/Clang 结果，改变普通 MSVC 的旧版完整块策略。
旧版平台检测未选择 MSVC 的 `_M_IX86`、`_M_X64` 或 `_M_ARM64` 宏
（见 [Microsoft 预定义宏](https://learn.microsoft.com/en-us/cpp/preprocessor/predefined-macros)），
导致小端主机进入无条件字节交换回退分支。公开宏 `BYTESWAP32` 和 `BYTESWAP64`
仍然保留，但不再控制这些哈希方法。调用方须重新编译，才能使用新的头文件行为。

| 方法族 | 默认结果 | 普通 MSVC 的旧结果 |
| --- | --- | --- |
| MurmurHash3 x86-32 | `murmur_hash3_x86_32` | `murmur_hash3_x86_32_legacy_msvc` |
| MurmurHash3 x86-128 | `murmur_hash3_x86_128`（实例方法） | `murmur_hash3_x86_128_legacy_msvc`（实例方法） |
| MurmurHash3 x64-128 | `murmur_hash3_x64_128` | `murmur_hash3_x64_128_legacy_msvc` |
| MurmurHash2 | `murmur_hash2`；在小端平台上保持不变 | 使用同一方法，无需独立旧版入口 |

三个 `_legacy_msvc` 方法在所有支持的平台上复现旧 MSVC 结果：完整块按大端方式读取，
尾部按小端方式组装，输出字按小端方式序列化。调用时须使用原有变体、准确输入字节、
长度和种子。输入短于完整块时，两种策略结果相同：x86-32 为不足 4 字节，
两个 128 位变体为不足 16 字节。对于字节 `00 01 02 03` 和种子零，x86-32 默认产生
数值字 `0xf4c0ec39`，旧版入口产生 `0x2cb313f3`。旧版大端使用方还须处理输出从
原生字节序变为规范小端字节序的变化；`_legacy_msvc` 专用于复现旧 MSVC 行为。

### 迁移已存储键与路由

框架内置 `ark::consistent_hash` 当前使用 MurmurHash2，在小端平台上的结果保持不变。以下路由迁移说明适用于使用受影响 MurmurHash3 策略构建环或分片键的调用方。

升级前，先确认每个数据集的哈希变体、种子、输入编码和旧编译器策略，并在数据中或
显式 schema 版本中记录该策略。单独的摘要无法还原原始键。由旧 MSVC 哈希派生的 ID、
分片映射、缓存键和持久化索引需要显式迁移，不能替换函数后直接重算路由。

对于查找键，分阶段采用**双读、新写**：新记录使用标准策略并携带版本标记；读取时
先查新键，未命中再查匹配的 `_legacy_msvc` 旧键。解析命中或哈希冲突时，核对原始逻辑键。
按应用的事务或原子操作规则回填或移动旧记录，明确重复记录的处理方式；确认存量清理和
回滚要求后，再移除旧读路径。开始只写新格式前，旧读程序须能理解新记录。
库本身不会自动迁移应用记录。

对于一致性哈希或分片归属，规划迁移时保持所有路由器使用同一个显式环版本。
从权威节点标识构建新环，比较分配结果，复制或重平衡受影响数据，再协调各参与方的
路由 epoch 与归属切换。数据转移验收前，保留旧环和旧哈希入口。普通二进制滚动升级中
混用新旧环，可能把同一键路由到不同位置；仅增加双查不会完成归属迁移。

`check-core` 纳入[上游 SMHasher 验证值](https://github.com/aappleby/smhasher/blob/master/src/main.cpp#L67-L70)
及其[前缀和种子验证过程](https://github.com/aappleby/smhasher/blob/master/src/KeysetTest.cpp#L11-L38)，
并为旧 MSVC 策略保留独立的标量参考固定值。用例覆盖全部七个公开方法、MurmurHash3 的
全部尾部长度、完整宽度种子、高位字节和旋转路径、MurmurHash2 块边界固定值，以及
偏移 0 到 7 的精确分配字节缓冲区。AddressSanitizer 可检查分配末端越界读取，
UndefinedBehaviorSanitizer 可检查无效对齐。固定值验证哈希行为；应用数据迁移仍须独立验收。

## 验证

`check-typed-examples` 与 timers、module_lifecycle、entity_operations 一起加载真实 Utility DSO，验证四个接口的 RTTI 注册键与管理器归属、GUID 生成与解析、zlib/Zstandard 二进制往返、由所属线程时间推进触发的定时器，以及正常和重复停机。`check-app-presets` 使用仓库启动器预设；应用、示例、Kernel、Utility 开启时，`check-core` 包含这两组检查。独立 `tests/consumer` 项目的 `check-sdk` 在关闭运行时插件时编译公开接口与辅助头路径，并拒绝可见的实现或装配头。这些检查验证正确性，不代表性能或任意回调生命周期验收。

`check-core` 还包含随机数头文件的独立编译，以及极值、异号、逆序和相等端点的确定性随机范围回归。构建命令见[核心回归](../../tests/core/README_CN.md)。
