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

接口借用于插件；定时器成员回调还借用接收者，均须存活至使用／回调完成或移除。所属线程用毫秒时间戳推进管理器，创建定时器不推进时间。后台 scheduler 保留 16 个回调工作线程和一个分发线程。按[模块生命周期](../guides/modules_CN.md)完成收尾，回调接收者须存活至调度器排空。

安装顺序仍为 GUID、timer、schedule、compress，卸载相反；timer `pre_shut()` 关闭时间轮。GUID 布局、压缩级别／算法及调度行为不变；`ark_utility_plugin` 保留平台后缀及 Debug `_d`。

## 调度器停机

调度模块使用现有生命周期钩子。`stop_accepting()` 关闭新的外部注册和后续重复调度分发。
现有返回 `void` 的注册方法忽略被拒绝的请求。注册、停止与分发通过同一互斥量确定顺序：
已接纳的单次 `in`/`at` 调度保留原定时间，在排空期间执行；已提交到线程池的回调继续执行，包括排队中的回调。
未来的重复调度释放所持资源，不再分发。已分发的 interval 回调执行完毕后不再重新注册。
正常运行时的间隔、指定时间和 cron 行为不变。指定时间和 cron 的日历转换使用调用方独占的
`std::tm` 存储；解析方式、进程本地时区选择以及 `mktime`/DST 规则保持不变。

排空期间，已接纳回调可在同一调度器注册单次后续操作，捕获对象的析构过程也保留这一权限。
该权限不适用于其他调度器或重复调度注册。调度模块还允许管理器所属线程在已接纳任务的轮询上下文中
调用单次 `add_at_schedule()`；所属线程在该上下文外的调用仍被拒绝。
模块在这些托管任务排空前保留分发线程；直接轮询原始任务 runtime 不授予该权限。

`ready_to_shut()` 提供非阻塞状态快照。所有已接纳的单次调度完成、未来重复调度释放、
注册操作完成捕获对象转移，并且所有已分发回调的持有者完成析构后，才返回 true。
捕获对象在调度器互斥量外析构，且析构先于就绪状态发布；析构过程中可重入停止和就绪查询。
回调异常报告到 stderr，并在工作持有者释放前完成析构；标准异常保留 `what()` 原因文本，
非标准异常使用通用消息。接口不暴露回调结果 future。分发提交、重复调度时间计算或重新插入发生异常时，
尚未转交的持有者通过已有节点返回调度器存储，不再分配内存；就绪状态保持关闭，不自动重试。重复停止是安全的。
该范围仅覆盖调度器持有的捕获对象，不包含调用方参数或外部生产者；销毁模块及回调接收者前须先停止这些生产者。

`pre_shut()` 在尚未就绪时返回 false，就绪后才由所属线程等待线程池退出。管理器保留现有的
全部模块就绪门槛和固定停机截止时间。已接纳的远期单次调度可能触及该截止时间，不会被静默取消。
超时后，即使工作随后完成，清理阶段仍保持关闭。
就绪查询不能中断回调或析构函数：两者须协作完成，期间仍需提供其依赖的所属线程更新。
直接持有 scheduler 的代码也可使用这三个方法。未先成功排空便析构时，调度器同步关闸并等待
已接纳工作完成，不受截止时间约束。不得在调度器自己的回调中销毁调度器，也不得让回调等待
所属线程完成 join。线程不会被分离或强制终止。这是调度器排空约定，不是通用 DSO 卸载或热更新屏障。

**二进制兼容性：** `scheduler.hpp` 为公开辅助类型 `ark::scheduler` 增加停机计数状态，改变对象布局。
Utility 插件与所有构造或内嵌该辅助类型的使用方须一起重新编译。`schedule_module` 接口和基类模块虚函数保持不变。

## 定时器所有权与分配

时间轮持有所有已分配的定时器，包括取消后仍留在轮槽中的条目。关闭或析构会释放
对象池与轮向量；重复调用 `shut()` 是安全的。在所属线程使用和关闭时间轮，并先完成
回调。轮槽精度保持为 10 ms，间隔仍向下取整到完整轮槽；回调顺序、重复次数和取消
生效时机不变。

`ARK_NEW` 和 `ARK_DELETE` 覆盖仍是分配边界。分配返回空指针时保留 Debug 断言；
关闭断言后，初始化失败会保留原有时间轮，时间轮或新定时器不可用时 `add_timer()`
返回 `0`。定时器模块会向正常启动流程返回 `false`，报告时间轮初始化失败。
标准容器的分配异常仍可能向外传播。**源码不兼容变更：**
`time_wheel_manager` 显式禁止复制和移动。直接使用该类型的调用方须通过单一所有者
将其保持在固定位置，例如由 `std::unique_ptr` 持有，不能复制或移动时间轮。
该公开低层类型的对象布局也有变化，库与所有直接使用 `time_wheel_manager` 的代码
须一起重新编译。`timer_module` 的虚接口保持不变。

## 压缩缓冲区与并发

`<ark/compress/zlib.hpp>`、`<ark/compress/zstd.hpp>`、`<ark/compress/lz4.hpp>` 每次调用持有独立堆缓冲。成功追加输出，拒绝输入则不改输出；调用方提供可读输入并同步共享输出。

| 压缩边界 | 契约 |
| --- | --- |
| 压缩 | 算法最坏输出必须严格小于 1 MiB |
| 解压 | 输出最多 1 MiB，含恰好 1 MiB；Zstandard 解码前拒绝超限、未知大小或无效帧 |
| 失败 | 无效数据返回 `false`；LZ4 空块解压仍返回 `false` |

<Aside type="caution" title="字典上下文仍需串行协调">

普通 zlib／LZ4／Zstandard 调用无共享可写输出；字典调用仍修改缓存上下文。每线程独立 `zstd_compress`，或串行执行字典初始化／调用／析构。初始化、所有权、复制合同不变，不保证同实例字典并发或插件卸载安全。

</Aside>

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

`check-timer`（`check-core` 的依赖）验证时间轮顺序、重复、取消、资源回收与分配失败恢复。
可单独执行 `cmake --build <build-dir> --target check-timer`。该目标还以 `NDEBUG`
独立编译生产定时器模块，通过公开接口验证分配失败的上报。

`check-scheduler-drain` 纳入 `check-core`，使用实际调度器和调度模块实现，通过受控回调及捕获对象屏障验证
已接纳和排队工作、重复调度释放、注册与停止竞态、同实例和托管任务后续操作、回调重入、捕获对象与异常析构、所属线程推进完成，以及管理器超时后不进入清理。
可单独运行 `cmake --build <build-dir> --target check-scheduler-drain`。
这些检查不构成通用 DSO 卸载、托管平台或生产容量验收。

该目标还运行独立分配失败测试：先使初始分发线程静止，再对实际 `dispatch()`/`manage_tasks()`
注入持续分配拒绝或重复调度异常，验证持有者仍被保留且就绪状态关闭，最后通过明确的测试专用恢复终止测试实例。
该流程不构成生产环境分发异常后的恢复能力证明。
