# 核心回归测试

[English](README.md)

doctest 程序验证字符串增长与比较、哈希键、对象所有权、常量迭代器、类型转换、
模块类型查询和成员函数回调。依赖初始化见[网络构建](../../docs/networking_CN.md)。

```sh
cmake -S . -B core-build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_TESTS=OFF -DARK_BUILD_CORE_TESTS=ON
cmake --build core-build --target check-core
```

运行 `core-build/tests/core/ark_core_tests --help` 查看 doctest 的筛选参数。
启用 `ARK_BUILD_CORE_TESTS=ON` 时，`check-network` 也会运行这些用例，现有 CI
矩阵会执行该入口。测试不需要数据库服务。

字符串匹配回归覆盖空分隔符、空替换模式、有限与无限次数、空输入、已有输出元素和内嵌空字符，并保留非空分隔符、剩余后缀、替换次数与删除语义。执行 `ark_core_tests --test-case="*String utilit*"` 可单独运行。参见[字符串匹配规则](../../docs/reference/utility_CN.md#core-字符串匹配)。

C++20 语言回归验证六种精确整数类型分类、限定符与引用处理、常量值、异常契约和固定 UID 位分配结果。独立 array-map 头文件检查还会拒绝重新引入旧语言兼容宏。

UID 回归另行验证编译期分配、非法及极端位宽、字段溢出和 63/0/0 的三种位宽排列。执行 `ark_core_tests --test-case="UID*"` 可单独运行。非法布局抛出 `std::invalid_argument`，非法字段值抛出 `std::out_of_range`，不受断言开关影响。参见 [UID 位分配](../../docs/reference/utility_CN.md#core-uid-位分配)。

随机数回归将公开头文件放在首个包含位置，并重复包含两次。测试保留固定的原始、有符号及无符号随机序列、最终种子和生成次数，并验证 `INT_MIN`／`INT_MAX` 极值、异号、相邻、逆序及相等端点的有符号半开区间。端点相同时直接返回该值，不消耗随机数。这些用例属于 `ark_core_tests` 和 `check-core`；使用 `-fsanitize=undefined -fno-sanitize-recover=all` 构建时，还会通过 UndefinedBehaviorSanitizer 检查范围运算。参见[随机数工具行为](../../docs/reference/utility_CN.md#core-随机数工具)。


Murmur 回归包含独立的上游 SMHasher 验证常量、旧版完整块交换策略固定值、全部尾部长度、
完整宽度种子和原有旋转路径。用例还验证 MurmurHash2 固定值、精确分配的输入缓冲区、
输入和输出偏移 0 到 7，以及输出保护字节。运行核心测试程序时，通过
`--test-case="*Murmur*"` 选择这些用例。规范小端输出和显式旧 MSVC 迁移入口见
[Murmur 兼容性说明](../../docs/reference/utility_CN.md#core-murmur-哈希)。
AddressSanitizer 和 UndefinedBehaviorSanitizer 构建可检查越界读取和无效对齐。

现有公开头映射检查继续保留。新增的纯编译检查仅映射 `<ark/hash/murmur_hash.hpp>`，
使用 C++20，不引入项目、平台、spdlog 或 Boost 包含路径，也不依赖 SDK 目标。
该检查纳入 `check-core`，也可独立运行，无需配置完整依赖图：

```sh
cmake -S tests/core/hash_header -B hash-header-build -G Ninja
cmake --build hash-header-build --target check-murmur-hash-header
```

头文件独立性不改变 `ark::core` 或其 CMake 包所需的依赖。

公共头检查将 `<ark/error_code.hpp>` 放在首个包含位置，并重复包含两次；编译期断言
验证其无符号 32 位底层类型及所有已声明的错误值。

启用 `ARK_BUILD_PROTO=ON` 时，还验证 Protobuf 解析失败、Actor ID、二进制消息、
旧版报文字节和对外 JSON 字段名，以及新的 `ark::protobuf` 描述符和枚举名称；
需要按应用构建说明初始化 Protobuf 依赖。

`ARK_BUILD_CONFIG=ON`（核心测试默认启用）时，`check-application-manifest`
验证 TOML / XML 等价性、默认值、严格字段、无效输入、诊断和拥有型结果，并纳入
`check-core`。参见[应用插件清单](../../docs/service-manifests_CN.md)。

同时构建应用时，`check-app-manifest-failures` 使用真实启动器验证文件缺失、语法错误和
字段类型错误的清单，要求正常返回退出码 1 并输出清单诊断。该入口覆盖 Windows，
并纳入 `check-core`。

`check-app-startup-failures` 使用真实启动器和测试动态库，覆盖四个启动阶段各自返回 false 或抛出异常，并分别读取 TOML 和 XML 清单；还验证非标准异常、插件入口抛出异常、库文件缺失，以及可加载但没有 `DllEntryPlugin` 的动态库。每次受控模块失败或插件入口异常前，已注册的测试模块都会启动并保留一个真实的 owned loopback TCP 监听端点，使用系统分配的端口。缺少入口的测试库会记录自身初始化，确认库已加载后才发生失败。24 个子进程均须输出一次准确的阶段、模块或插件诊断，保留可用异常信息，以正常退出码 1 结束，且不执行后续启动调用、update、正常关闭或卸载。核心用例检查失败上下文、原始异常传播、多模块中的首次失败停止及所属线程访问。若失败启动后仍保留模块所有权，该端点会使普通静态清理等待，因此退出检查覆盖真实的活动 IO 池。这些测试证明失败报告与进程终止，不证明部分启动资源清理或同进程恢复。

启用 Protobuf 时，`check-core` 还包含 `check-proto-configuration`。该检查执行两个平台分支的生产 target 配置，核对传递给消费者的 DLL 导入定义和运行时目录，并通过真实 protoc 输出验证消息／描述符导出及原有无导出标注路径。生成 fixture 使用 Ninja，不编译 Windows 二进制。真实 Kernel DSO 测试保留宿主持有的生成 Protobuf 消息和跨 DSO 转换，继续覆盖公开 DLL 边界。

`check-kernel-initialization` 从 `resource/meta/` 加载真实元数据和静态数据，
分别使用资源相对路径及以 `/` 开头的旧路径，验证内核与配置模块的四种初始化顺序，共八种组合。

`entity_nested_snapshot.cpp` 覆盖拥有型根／表格／容器捕获、显式子授权、Schema 局部索引、行号／GUID 复用、清空与回调异常、聚合上限、重复引用／环及存续期计数不回绕。行分配／插入失败测试同时检查失败添加不消耗标记。`check-kernel-plugin` 经真实已加载虚接口捕获嵌套状态，并在正常停止后保留值。

`entity_snapshot.cpp` 使用真实 Entity／定义对象，检查独立拥有的显式标量捕获、全部受支持原生类型、整数／GUID 边界、有限浮点值、二进制字符串、不创建节点的惰性零值、隐藏字段排除、源销毁、限额，以及失败后源保持不变。头文件消费者还在关闭 Protobuf 时编译独立的快照值契约。

捕获测试通过 `kernel_module` 指针调用。`entity_snapshot_interface.cpp` 保留一份只实现原有纯虚接口的外部源码实现，检查它仍可实例化、默认方法抛出 `logic_error`、自定义覆写及框架覆写。

`check-entity-stream` 纳入 `check-core`，无需 Protobuf 即可验证标量发布端与接收端，覆盖保留的不可变状态、合并、重复与缺口、身份与类型检查、移除、更高代次恢复和限额。Entity 测试还捕获真实 HP／MP／名称字段，应用基线与增量，销毁源对象后验证保留值，并在删除标记后恢复。这些是 C++ 状态检查，不代表网络交付或 Godot 验收。
`entity_nested_stream.cpp` 检查固定可信的逐类／列／子绑定、递归整根原子补丁、行／Entity 复用及有界身份历史。`entity_nested_stream_allocation.cpp` 对发布更新／删除、接收基线／候选构造的分配点注入失败，要求抛出分配异常时旧状态／修订不变且可重试。真实嵌套 Kernel 捕获检查两个固定投影、行复用／清空及子移动／销毁；这些不是网络交付验收。


同一 target 运行 `kernel_delayed_sync.cpp`：真实容器创建、移除和销毁经过延迟收集、Kernel 更新、Protobuf 序列化和既有掩码交付回调。用例覆盖完整载荷、实体引用、空槽／销毁标记、仅自身同步掩码，以及空批次、标量、表格和混合对照。范围受限的测试友元仅观察该回调，不提供传输层或客户端。

延迟交付用例还在交付回调中收集标量／表格／容器变更，验证递归刷新被拒绝，以及回调异常后防护复位、新变更保留。整个过程保持借用节点存活。

`check-logging` 运行独立的 C++ 日志路径回归测试，并由 `check-core` 一并执行。
用例覆盖相对路径、嵌套路径、扩展名、隐藏文件、含点的目录名、两个按小时轮转的日志创建函数，
以及配置中的日志目录。`output_path` 为空时使用当前工作目录。按小时轮转的日志仍采用
`目录/YYYYMMDD/文件名_hh.扩展名` 布局；只有文件名的最后一个扩展名放在小时后缀之后。
没有扩展名或以点结尾时保留完整文件名。测试只在临时目录写入文件。

```sh
cmake --build core-build --target check-logging
```

## 压缩辅助函数

`ARK_BUILD_COMPRESSION_TESTS=ON` 将 `check-compression` 加入 `check-core`，也支持单独
运行此目标。通过 vcpkg 的 `compression-tests` feature 安装 zlib、Zstandard 和 LZ4。
该选项默认为 `OFF`，保留仅构建核心时的既有依赖要求；Applications CI 会显式开启。
检查会独立编译每个映射后的 `<ark/compress/...>` 头，覆盖二进制和空输入、追加语义、
严格的压缩大小边界、恰好 1 MiB 的解压、超限或未知大小帧、损坏帧、整数长度拒绝和
并发往返。字典工作线程使用独立实例。调用方输出字符串和字典上下文的同步要求见
[Utility 压缩说明](../../docs/reference/utility_CN.md#压缩缓冲区与并发)。

```sh
cmake --build core-build --target check-compression --parallel 2
```

## 原生动态库关闭结果

`check-native-loader` 构建独立宿主和两个真实共享库。加载的库调用仅位于其自身目录的依赖；宿主和工作目录分别位于其他目录，插件目录不加入 PATH。测试分别加载正斜线绝对路径和相对路径，调用导出符号并对每个句柄重复关闭。Windows 还检查原生失败码和非法 UTF-8 诊断。配置检查确保宿主不链接 fixture 库，并保持输出目录分离。此 target 是 `check-native-unload` 的依赖，因此也纳入 `check-core`。非 Windows 运行验证真实共享库加载，不代表 Windows 依赖搜索验收。

`check-native-unload` 纳入 `check-core`，复用现有 OSS SDK 入口探针，不需要 OSS 服务或运行时应用构建。确定性测试通过私有的单次调用适配器，对有效的已加载句柄注入原生失败结果，不增加生产全局钩子或公开虚拟模拟接口。检查覆盖平台成功约定、失败句柄／包装对象保留、停止后续关闭、插件出口仅执行一次与管理器失败状态保持。成功关闭后清空句柄；空对象或已成功关闭的对象重复卸载，不再调用操作系统。独立正常路径执行真实插件入口／出口和原生关闭 API。这些检查不证明操作系统已移除库映像、真实数据库验收或任意热更新安全。

```sh
cmake --build core-build --target check-native-unload --parallel 1
```

## 公共任务接口

`check-core` 包含 `check-task-sdk`：独立编译仅包含 `<ark/task.hpp>` 的文件，
并运行使用 `ark::core` 的普通调用方。测试覆盖拥有型返回值、停止受理、显式取消
和所属线程检查。受控子进程检查要求在仍有任务或跨线程析构时以代码 77 结束，
不会在测试所属对象中直接放弃挂起的协程。私有数据库任务测试通过别名使用同一个
公开实现。参见[协程任务](../../docs/guides/tasks_CN.md)，了解可独立运行的
`ark_task_example` 和显式生命周期要求。

## 共享业务执行器

`check-core` 包含 `check-business-runtime`：独立编译仅包含
`<ark/plugin_manager.hpp>` 的调用方，验证单个共享执行器、统一 handler 预算、
模块完成通知先于轮询、停止受理后的连续等待、迟到结果、所属线程与重入检查、
有任务时的模块注销、组引用稳定性和失败诊断。真实注销宏必须先抛出错误，不能删除
仍有任务的模块。四个受控子进程验证有任务、跨线程、更新栈内和最后完成回调内析构。
测试不需要数据库服务。

```sh
cmake --build core-build --target check-business-runtime
```

管理器用例证明受管理业务任务的排空；下面的启动器用例还验证一个受控原生线程退出屏障，二者均不证明通用动态库卸载安全。
参见[模块任务集成](../../docs/guides/tasks_CN.md#模块共用业务执行器)。

同时构建应用与配置读取器时，`check-app-business-runtime` 在真实启动器中加载测试专用原生插件。POSIX 用例请求 SIGTERM，Windows 向真实命令线程输入 `exit`。任务排空用例中，已受理任务在排空阶段收到两次受控私有桥接结果；任务组仍有空余容量时也拒绝新根任务。最后完成回调返回后，owner frame 停止原生工作并等待线程退出、join，随后才进入关闭与卸载。默认无限等待和配置正数时限的正常排空均须返回退出码 0。另有关闭钩子返回失败或抛出异常、update 抛出标准或非标准异常、任务停滞、任务或原生线程延迟完成的子进程，要求诊断只输出一次、模块仍保留，并存活至测试观察时限。更新异常轨迹必须包含抛出、owner 更新恢复和已受理任务完成；Windows 不能将这些已处理的 C++ 异常送入原生崩溃对话框。原生线程就绪超时用例使用空任务组和受控等待的原生线程，避免协程调度消耗其 50 毫秒时限；轨迹与诊断必须确认待完成任务为零，并处于就绪等待阶段。配置的应用等待时限到达后，后续完成不得恢复卸载；非法、负数和溢出的 CLI 时限必须在加载配置前拒绝。应用时限仅在 owner loop 返回时检查，不能中断阻塞中的用户更新、就绪检查或关闭调用。测试观察时限只终止测试自己创建的子进程，不表示成功停机或生产策略；测试不使用真实数据库。

开启应用、示例、Kernel 和 Utility 后，`check-typed-examples` 加载真实 Utility DSO，验证四个接口、接口 RTTI 注册键、GUID 生成与解析、zlib/Zstandard 二进制往返、由管理器时间戳推进的定时器，以及正常和重复停机。`check-app-presets` 同样加载仓库 Timers 运行包；两者均为 `check-core` 的依赖。task/scheduler 包含顺序检查通过 `ark::utility` 获取映射后的 scheduler 头，独立的 task-only 公开接口检查仍只依赖 `ark::core`。

启用 Net、Bus、Utility 与 Protobuf 时，`check-net-plugin` 加载真实动态库，验证两个 Net 接口及 RTTI 键、管理器线程上的 TCP／HTTP 回调、重复停服与监听端口释放。该目标也是 `check-core` 的依赖，无需外部服务。独立的 `tests/consumer/net` 工程仅链接 `ark::net`，逐个编译保留的公开头。

启用 Bus、Net、Utility 与 Protobuf 时，`check-bus-plugin` 加载真实动态库，验证 Bus／消息接口键、配置查询、路由 Protobuf 消息头与正文、传输失败传播及正常重复停服。`check-core` 包含该目标。独立的 `tests/consumer/bus` 工程仅链接 `ark::bus`，分别编译两个公开模块头。既有解析／配置用例通过接口工厂保留原断言，仍支持关闭 Protobuf 的构建。

在 POSIX 平台启用应用、Bus、Net、Utility 和 Kernel 后，`check-server-startup-game` 使用私有 `Player` JSON 结构和 Bus 身份 `1.1.31.1`，启动真实 Utility、Bus、Net、Kernel 和 Game 动态库。健康场景要求真实 TCP 连接、配合信号停服、客户端连接关闭、新连接被拒绝，以及退出码 0。端口占用场景要求完整的 Game／`post_init` 启动失败诊断和退出码 1。Kernel 目标存在时，这两个场景纳入 `check-server-startup`、`check-server-plugins` 和 `check-core`。测试沿用现有有限等待，只拥有自己创建的子进程与回环端点，不检查 Game 回调注册、Entity、玩法、随仓库提供的预设、服务发现或生产就绪状态。Windows 保留独立的创建测试。详见[服务端架构](../../docs/concepts/server-architecture_CN.md#角色启动错误)。

启用 Game、Kernel、Bus 和 Utility 目标后，`check-game-initialization` 运行生产 Game 工厂／装配，以及真实 Kernel、Metadata、Map、Config、Guid 和 Bus 实现。测试先调用 Game `init()`，再调用 Kernel `init()`，覆盖 Game／Kernel `post_init()` 的两种顺序，并通过元数据事件分发观察真实 Player 类回调。另一份不含 Player 的合法结构必须在创建监听器前失败。测试只控制监听器创建，不替换 Kernel 接口。这些用例纳入 `check-kernel-initialization`、`check-server-plugins` 和 `check-core`。真实套接字启动与停服由上文的 POSIX DSO 场景覆盖；玩法和部分启动回滚不在测试范围内。

同一 `check-typed-examples` 还通过仓库清单和示例元数据加载实体操作 DSO，验证四个 Player 实体、原有字段／自定义值与物品行、没有计时循环输出的有界启动、编译期模块／update 契约、bag 回调分发、四次销毁事件、更新及正常／重复停机。显式的 `check-entity-benchmark-smoke` 是独立可选 target，不作为 `check-core` 的依赖。

启用 `ARK_BUILD_PLUGIN_HTTP=ON` 时，`check-http-plugin` 加载真实 HTTP 动态库并发送真实回环请求，覆盖所属线程停服期间的受控活动处理器退出与端点复用。可选 HTTP 目标存在时，`check-core` 包含该检查。独立的 `tests/consumer/http` 工程仅链接 `ark::http`，关闭应用与 Protobuf。

Applications CI 显式开启 `ARK_BUILD_PLUGIN_HTTP=ON` 并运行 `check-core`，因此既有源码检查与全矩阵均包含真实 HTTP 动态库回归。本地启用 HTTP 的原生验证已通过；本次变更的托管全矩阵仍待验收。永久 CI 策略测试检查配置到真实动态库回归的依赖链。

Kernel 的公开头通过 `ark::kernel` 从 `src/plugins/kernel/` 映射；`check-core` 保留数据与节点回归，`check-kernel-initialization` 保留初始化与配置复制断言。独立工程 `tests/consumer/kernel` 在 Protobuf 开启／关闭时消费公开头，关闭时排除三个依赖 Protobuf 的模块头；其中 `container_meta.hpp` 保留原有 `ark/define.hpp` 与 `ark/enum.hpp` 前置包含要求。详见 [Kernel 参考](../../docs/reference/kernel_CN.md)。

Net 注册的 HTTP 客户端现在由私有接口工厂创建。`check-net-plugin` 还验证无需管理器更新即可完成的 future，以及 DSO 仍加载时通过 `pre_shut()` 取消未完成请求；在正常插件退出前消费这些 future，不证明 future 可跨任意卸载继续使用。`check-network` 经由相同私有工厂和公开客户端接口保留请求、超时、回调与停机覆盖。

核心测试配置使用 Boost.Asio 且存在 `ark::http`、`ark::net`、`ark::etcd_client` 三个 API target 时，
`ARK_BUILD_HTTP_EXAMPLE_TESTS` 默认 `ON`，否则默认 `OFF`；显式设置 `OFF` 仍然有效。
选中后 `check-core` 包含 `check-http-examples`，编译真实旧 HTTP 示例模块及动态库入口。
既有 Applications CI 通过该依赖运行测试，默认应用示例集合不变。
测试通过独立的公开服务替身验证接口工厂／RTTI／配置、原有 POST／租约参数与超时默认值、
所属线程 update 回调派发、监听参数、租约析构及重复停机／重新加载，不向外部 HTTP／etcd 发请求。
保留的固定演示端点与真实传输验证边界见 [HTTP 示例](../../docs/reference/http_CN.md#旧-http-示例的类型化创建)。

开启 Protobuf 且存在 Kernel／Utility 运行时 target 时，`check-kernel-plugin` 通过独立公开接口 runner 加载真实 DSO，检查原有四个接口键与管理器指针、元数据／静态记录加载、地图／实体注册和回调、管理器更新、停机销毁与重复 stop。它与现有直接初始化／实体默认值测试一起纳入 `check-core`；不在卸载后检查注册表，也不证明任意热更新安全。Kernel 公开头消费者检查全部保留类型的基类和 update 声明。

该独立 runner 还通过加载 DSO 的接口指针调用 `kernel_module::capture_scalar_snapshot()`，覆盖空选择、全部标量类型、惰性默认值、原生宽度、限额，以及源修改和正常插件停止后快照值仍有效。runner 不把捕获实现编入自身可执行文件。此虚方法改变 Kernel 虚函数表 ABI，加载新接口前必须一起重建所有插件和宿主／消费者。

XXTEA 用例覆盖精确长度分配的短密钥、16 字节与内嵌零字节密钥、二进制往返及输出追加，参见[密钥约定](../../docs/reference/xxtea_CN.md)。

启用可选 Kafka 运行时（`ARK_BUILD_APPLICATIONS=ON`、`ARK_BUILD_PROTO=ON`、
`ARK_BUILD_CORE_TESTS=ON`、`ARK_BUILD_PLUGIN_KAFKA=ON`）并准备原有驱动依赖后，
`check-core` 包含 `check-kafka-plugin` 与 `check-kafka-construction`。专项检查编译真实
工厂／装配，验证接口 RTTI 元数据与启用 update，通过受控工厂执行生产入口／出口，
并加载真实生产动态库检查导出。测试不创建生产者、不连接 broker。Kafka 继续默认关闭；
关闭插件的核心测试不发现其依赖。公开布局重建要求与服务验收边界见
[构造迁移](../../docs/reference/directory-structure_CN.md#kafka-类型化构造)。

`check-startup-cleanup` 验证一个预登记 owning capsule、私有时间轮、四阶段失败、一次性清理与持续分配故障紧急退出；六个隔离子进程区分调用前失败与启动内部失败。启用应用时，还运行真实 launcher／独立测试 DSO 的 14 个场景。它不接入共享 Utility 或 IO，也不证明业务回滚；见[启动失败清理](../../docs/project/startup-failure-cleanup_CN.md)。

`check-scalar-stream-allocation` 独立运行全局分配失败注入，逐个遍历标量发布端构造、基线复制、更新、首次／替换基线接收和增量接收中的分配。失败保留已提交状态与旧拥有值，重试仅推进一次修订；用例包含长／二进制字符串及全部标量类型，已纳入 `check-entity-stream`。
