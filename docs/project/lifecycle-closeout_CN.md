# 启动与停机闭环提案

本文记录已选生命周期路线及其交付阶段。阶段 1 增加必需 provider 元数据、依赖图验证和有序启动。阶段 2 增加默认兼容、消费者优先的一次性 `stop_accepting()`；目前仅受控业务 fixture 迁移了根请求入口。官方入口迁移、按依赖反向完整清理和可选的启动失败补偿仍是后续工作。阶段 1 之前的源码审阅基准为 [988dfbc0](https://github.com/OpenArkStudio/ark/tree/988dfbc048425cdb56cbe5f14314320fe698d07d)。操作步骤见[应用启动器](../guides/applications_CN.md)。

## 当前启动状态与资源所有权

`plugin_manager` 只保存当前失败描述，没有逐模块的启动状态记录。插件和模块仍使用无序容器。安装完成后，管理器对声明的 provider 注册键生成快照，拒绝缺失依赖／依赖环，并在四个启动阶段使用同一稳定拓扑顺序。无依赖声明的模块视为独立节点，当前可启动节点按注册键字典序选择。清单顺序既不决定这一顺序，也不决定停机依赖。见[启动依赖与重建要求](../guides/modules_CN.md#声明启动依赖)。

| 已到达的边界 | 可能已存在的资源 | 失败行为 |
| --- | --- | --- |
| 参数解析与日志准备 | 管理器、共享 owner runtime 和默认日志对象；尚未加载应用插件 | 无效参数／帮助沿用退出码 `0`；清单错误返回 `1` |
| 加载动态库并进入 `DllEntryPlugin` | 库句柄、插件对象、已注册模块；安装阶段就可能启动 IO | 缺少库／入口、启动返回 false 或入口异常会立即停止后续启动 |
| 验证启动依赖 | 安装阶段资源继续存活；尚未运行模块启动钩子 | 缺少必需键／依赖成环时返回 false；元数据异常保留依赖上下文并向上传播 |
| `init()` | 所有已成功安装的插件，以及已完成或执行了一部分的模块初始化 | 首个 false 或异常立即结束当前阶段，不再调用该阶段的后续模块 |
| `post_init()`、`check_config()`、`pre_update()` | 已完成的前置阶段、回调、定时器、监听器、出站客户端和已提交工作 | 继续遵守首个失败即停止；不进入后续阶段 |
| `start()` 成功 | 所选模块已完成四个阶段 | 进入 owner 更新；Windows 命令线程也只在此后创建 |

资源必须按实际获取点判断：Net 的 HTTP 客户端在构造函数中启动 IO 池；Net 在 `post_init()` 中安排客户端连接和健康检查定时器；Game 在 `post_init()` 中注册 Kernel 回调并启动监听。数据库连接配置也可能创建原生池，不能只用模块最后一个成功完成的阶段推断资源状态。

启动失败时，启动器刷新带阶段和所属模块／插件的诊断，然后通过 `_Exit` 以退出码 `1` 结束；Windows 还调用 `TerminateProcess`。该路径绕过对象和静态析构，不运行正常停机钩子，也不重试。已加载但缺少入口的动态库保留至进程退出。`register_plugin` 在 `install()` 前释放局部候选对象的所有权，安装返回后才记录插件；只记录成功安装，会遗漏安装过程中已发布、随后又抛出异常的资源。这是清理设计的约束，不是现有回滚保证。

## 当前正常停机

| 步骤 | 条件与动作 | 必须继续存活的资源 |
| --- | --- | --- |
| 请求停机 | 关闭受管根任务准入，进入 `stopping_acceptance`；活跃回调栈尚未退出时推迟模块钩子 | provider、服务、bridge 和共享 owner runtime |
| 停止新外部根请求 | 在外层 owner frame 按缓存拓扑反向执行一次实际到达的 `stop_accepting()`，随后进入 `draining` | 已接收流程保留 provider 操作与完成通知；本阶段不回收池／传输 |
| 完成已接收任务 | 继续执行启用的模块更新，再对 owner runtime 做一次有处理器数量上限的 poll；不取消已接收工作 | 已接收任务的后续 await 仍可提交 provider 操作 |
| 退出活跃调用栈 | 任务数归零后，仍须等待 update／poll／最终完成回调退出 | 最终完成回调执行前，任务已经从组中移除 |
| 检查所有模块 | 所有 `ready_to_shut()` 返回 true 后，才能运行任何 `pre_shut()` | 返回 false 则继续等待和 owner 更新；抛出异常则进入不可恢复的 `failed` |
| 释放资源 | 依次遍历全部 `pre_shut()`、全部 `shut()`、插件出口／卸载，最后关闭原生库句柄 | 每个钩子返回前，其依赖必须保持有效 |
| 结束 | 全部成功，重复 `stop()` 返回 true | 同步输出停机消息，以退出码 `0` 结束 |

当前模块钩子为 `ready_to_shut()`，本文不新增 `drain_ready()` API。默认钩子返回 true，不能证明未受管工作已经结束。新 `stop_accepting()` 阶段默认返回 true，本身不完成官方入口迁移。它采用缓存拓扑的反向顺序；就绪检查、`pre_shut()`／`shut()` 和插件／库释放仍保留原有顺序。停机遍历不保证消费者与 provider 的先后关系；单个插件内显式注销的顺序也不能约束不同插件。

### 原生工作与保留规则

| 所有者 | 现有完成／回收路径 | 边界 |
| --- | --- | --- |
| 受管任务 | `task_group::stop_accepting()`、模块服务结果发布、共享 `manual_runtime::poll()` | 任务数和处理器数量上限不代表原生线程已退出，也不代表停机耗时上限 |
| MySQL／Redis | `ready_to_shut()` 观察服务和 bridge 静止；`pre_shut()` 关闭接收入口并回收原生资源；`shut()` 确认 `native_retired()` 后清除连接 | 原生回收调用固定版本 arknet 池的 `close_and_wait()`；旧服务 `stop()` 只请求关闭／抑制回调，不提供同等屏障 |
| Net HTTP 客户端／路由 HTTP 服务端 | 在 `pre_shut()` 中同步停止池／服务端；传输包装器在析构时停止 | 调用可能阻塞；模块默认就绪不能独立证明全部回调接收者和外部保留句柄都已退出 |
| Net 服务管理器 | 持有监听器、客户端和回调；当前 `shut()` 没有显式回收这些服务 | 对象析构和已有 DSO 测试覆盖具体释放路径，不构成通用的依赖感知回调屏障 |

共享 manual runtime 在 owner 线程运行用户处理器；原生 IO 池保留自己的线程。对应回调返回、所属原生资源回收屏障完成之前，不得销毁所有者或卸载其代码。`dlclose`／`FreeLibrary` 成功不证明库映像已从内存移除。

默认停机无限等待。正数 `--shutdown_timeout_ms` 通过 `stop_until()` 传入单调时钟截止时间，并在后续 stop 调用中保留。停止接收／就绪钩子返回后及 `pre_shut()` 前检查时间，慢钩子成功且没有待完成任务时，也不能跳过保留。清理前超时进入不可恢复的 `timed_out`，允许 owner 更新，但后续完成不能恢复清理。清理开始后，钩子／关闭调用返回边界检查到超限，进入 `cleanup_timed_out`，保留剩余资源，停止后续更新和清理。检查不能中断阻塞的 update、钩子或原生关闭，也不能回滚调用返回前已经执行的动作。排空期间的 update 失败同样锁定启动器保留状态。钩子／关闭失败进入不可恢复的 `failed`，停止更新和后续清理。任何超时都不授权强制取消、销毁待完成帧或按成功退出。

## 已选路线与后续实施

1. **先记录资源获取，再增加清理。** 内部记录已加载库、安装及模块阶段的进入／完成状态，以及资源补偿动作。阶段必须同时记录「已进入」和「已完成」，因为失败的钩子也可能获取资源。保留首个启动失败为主要诊断，清理失败单独报告。显式接入的模块在获取资源时登记补偿动作；没有可验证的清理动作时，继续使用现有失败退出策略。不得对未完整初始化的模块统一调用正常停机钩子，也不增加同进程重启。
2. **按模块声明 provider。** 阶段 1 提供 `module::get_dependencies()` 和 `module_base<interface, providers...>`，在安装完成、`init()` 开始前验证全部声明键和依赖环，并让每个启动阶段先执行 provider。首个迁移应用为 Timers 加 Utility；没有凭猜测给其他官方模块批量添加依赖。依赖图验证注册键，不验证任意原始 `add_module()` 条目的动态类型。用于停机前，仍须根据实际保留的指针／回调补齐声明。例如，Game 保留 Kernel／Guid／Bus／Msg／Net／Map；Net 服务管理器保留 Bus／Timer；Kernel 保留 Map／元数据／配置／Guid。安装阶段已经可能获取资源，所以依赖验证不能替代获取点清理。声明安全停机之前，还须显式处理回调所有权、缺失的生命周期依赖和阶段间依赖环。
3. **分离外部请求停止接收与 provider 回收。** 阶段 2 已提供显式的模块停止接收动作，阻止新的外部业务根任务，同时保留已接收任务所需的完成通知和后续 provider 调用。钩子按消费者优先顺序执行一次，保留默认兼容和不可恢复的失败状态。成功启动后缓存拓扑；手动管理器或合法 idle registry 变化会在首个钩子前触发一次重建／验证，不在 poll／重入中重读，声明在注册期间应保持稳定。现有全局任务／活跃栈和全部模块就绪检查继续保留；按消费者优先顺序排列 `pre_shut()`／`shut()` 属于后续阶段。如果一个模块的就绪依赖另一个模块先清理，就出现了阶段间依赖环；应在更早的停止接收动作中解除，或将阶段变更另行提交确认。清点回调接收者、定时器注册和跨插件捕获；依赖插件代码的对象与 callable 全部退出后，才能卸载插件并关闭库。失败或等待超时后，保留现有不可恢复的资源保留规则。按完整迁移集合显式启用，不静默改变旧钩子的含义。

这些是内部生命周期改进，不涉及 Registry、热更新、服务发现或网络协议。

## 交付阶段

路线已经选定；下表区分实施范围，不再将其列为待确认决策。

| 已选方向 | 当前交付 |
| --- | --- |
| 模块级必需 provider 声明、缺失／环验证与有序启动 | 阶段 1；已迁移 Timers／Utility，无声明模块仍作为独立节点 |
| 独立停止接收，已接收任务继续访问 provider | 阶段 2 Core 合同和受控 fixture；官方 Net／HTTP 入口仍需要合适的原生 API |
| 逐模块资源／阶段记录，以及启动失败时的可选补偿，随后退出 `1` | 后续阶段；默认仍立即失败退出，不自动回滚 |
| 通过全部活跃栈／任务／就绪检查后，先消费者、后 provider 完整清理 | 后续阶段；不提供新的通用停机或卸载保证 |

## 现有测试与后续验收

[启动器测试](https://github.com/OpenArkStudio/ark/blob/main/tests/app/business_runtime.cmake) 保留以下原有 **13 个场景**，并增加四个停止接收场景，共 **17 个**。测试运行真实启动器和测试 DSO，使用受控协议完成通知以及真实原生线程；不代表真实数据库服务或任意插件组合验收。

| 场景 | 必须观察到的结果 |
| --- | --- |
| `drain`、`drain_deadline`、`drain_max_deadline` | 已接收的顺序 await 和最终回调完成，再发生原生线程退出／join、钩子、卸载／析构和退出码 `0` |
| `drain_admission` | 根请求入口关闭后，已接收根任务继续发起两次 await，最终完成、原生回收和清理成功；hook／重入 stop／拒绝根事件只出现一次 |
| `stop_accepting_false`、`stop_accepting_throw`、`stop_accepting_throw_unknown` | 阶段／模块只诊断一次；待完成帧、进程和资源保留，不关闭 provider，也不进入后续钩子或卸载 |
| `pre_shut_false`、`shut_false`、`shut_throw` | 首个清理失败阶段只诊断一次；不继续卸载／析构，也不重复清理 |
| `ready_throw`、`ready_throw_unknown` | 原生线程可以先退出，但就绪异常阻止 join／清理，诊断只出现一次 |
| `timeout_task`、`timeout_native`、`timeout_stalled` | 区分受管任务与就绪等待；晚完成也继续保留进程和对象；停滞工作始终不能授权卸载 |
| `update_throw`、`update_throw_unknown` | 后续 update 可以恢复，先前的排空失败仍阻止清理 |

测试用观察超时终止自己创建的保留进程，不代表框架正常停机。测试还检查四个无效截止时间参数。[启动失败测试](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/tests/app/startup_failures.cmake) 分别用 TOML 和 XML 检查 12 种失败模式，包括失败前已启动的真实监听器。[Core 启动测试](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/tests/core/plugin_manager_startup.cpp) 和[业务 runtime 测试](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/tests/core/business_runtime.cpp) 覆盖首个失败、回调／重入限制和全部模块就绪检查。

Core 停止接收测试覆盖缓存反向顺序、一次性／重入、手动 stop、合法 idle 新增／移除、同键替换、无效图，以及元数据／钩子失败。[模块指南](../guides/modules_CN.md#停止接收新业务根请求)记录固定版本 arknet 缺少独立停止接收 API 的边界；stop 服务端或修改 acceptor 都不能安全替代。

独立的 `check-app-shutdown-deadlines` 包含三个慢钩子成功返回场景，不依赖受管任务或原生线程等待：停止接收／就绪钩子超过截止时间后不能进入 `pre_shut()`；慢 `pre_shut()` 返回后不能执行 `shut()`／卸载或后续模块更新。进程持续保留，由测试终止自己创建的子进程。检查覆盖钩子返回边界，不代表抢占或回滚。

永久 Core 依赖测试覆盖 provider 缺失／自依赖／成环、传递／菱形图、重复边、独立节点稳定排序、元数据异常，以及每个启动阶段的首个失败终止。`check-app-module-dependencies` 用真实 Timers DSO 加载缺少 Utility 的应用，要求在 `init()` 前发生依赖失败，并由启动器以退出码 `1` 结束；已有类型化示例／预设测试验证完整应用。本地记录与托管验收继续分开。后续阶段仍须永久测试每个已进入阶段／获取点的补偿、跨插件回调生命周期，以及已接收工作后续调用期间 provider 持续存活；有序启动本身不提供这些停机保证。

阶段 1 之前的源码入口：[管理器](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/core/plugin_manager.hpp)、[启动器](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/application/main.cpp)、[任务组](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/core/task.hpp)、[Net 服务](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/net/net_service_manager_module_impl.cpp)、[MySQL](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/mysql/mysql_module.cpp)、[Redis](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/redis/redis_module.cpp)。原生池语义以配置固定的 [arknet 版本](https://github.com/OpenArkStudio/arknet/tree/a0dfe09cd543e75cfb5996e537ce2fa49f2a758f) 为准，不代表未经验证的依赖升级。
