# 启动与停机交付状态

已提供有序启动、一次性停止接收及已受理任务排空。完整依赖感知清理和共享 IO 启动失败清理仍待后续实施。

<CardGrid>

<LinkCard title="操作启动器" href="../guides/applications_CN.md" description="启动、停机与超时处理。" />
<LinkCard title="登记失败清理" href="startup-failure-cleanup_CN.md" description="独立所有权、16 个 owner 的预分配登记与 IO 接入前提。" />

</CardGrid>

## 当前启动状态与资源所有权

安装后，`plugin_manager` 按声明的 provider 键拓扑排列四个启动阶段；缺失依赖／环直接失败，无声明模块视为独立节点。可启动节点按注册键字典序选择，清单顺序不决定启动或清理依赖。插件／模块仍存于无序容器。管理器保留失败描述和有界的最近调用记录，不保存完整阶段历史。见[依赖与重建要求](../guides/modules_CN.md#声明启动依赖)。

| 已到达的边界 | 可能已存在的资源 | 失败行为 |
| --- | --- | --- |
| 参数解析与日志准备 | 管理器、共享 owner runtime 和默认日志对象；尚未加载应用插件 | 无效参数／帮助沿用退出码 `0`；清单错误返回 `1` |
| 加载动态库并进入 `DllEntryPlugin` | 库句柄、插件对象、已注册模块；安装阶段就可能启动 IO | 缺少库／入口、启动返回 false 或入口异常会立即停止后续启动 |
| 验证启动依赖 | 安装阶段资源继续存活；尚未运行模块启动钩子 | 缺少必需键／依赖成环时返回 false；元数据异常保留依赖上下文并向上传播 |
| `init()` | 所有已成功安装的插件，以及已完成或执行了一部分的模块初始化 | 首个 false 或异常立即结束当前阶段，不再调用该阶段的后续模块 |
| `post_init()`、`check_config()`、`pre_update()` | 已完成的前置阶段、回调、定时器、监听器、出站客户端和已提交工作 | 继续遵守首个失败即停止；不进入后续阶段 |
| `start()` 成功 | 所选模块已完成四个阶段 | 进入 owner 更新；Windows 命令线程也只在此后创建 |

资源归属按获取点判断：HTTP 客户端构造时启动 IO；Net 在 `post_init()` 排队客户端／定时器；Game 在该阶段登记 Kernel 回调并启动监听；数据库配置可创建池。最后完成的钩子不能证明资源寿命。

<Aside type="caution" title="启动失败后退出进程">

启动器报告阶段／所有者，只执行已登记的独立清理，然后通过 `_Exit(1)` 退出；Windows 另调用 `TerminateProcess`。不执行正常钩子、对象／静态析构或重试。缺少入口的库保留至退出。`register_plugin` 在 `install()` 前释放候选、安装返回后才记录成功；该表无法覆盖安装抛异常前已发布的资源。

</Aside>

## 当前正常停机

<Steps>

<Step title="请求停机">

关闭受管根任务准入，进入 `stopping_acceptance`；活跃回调栈尚未退出时推迟模块钩子。

保持：provider、服务、bridge 和共享 owner runtime

</Step>

<Step title="停止新外部根请求">

在外层 owner frame 按缓存拓扑反向执行一次实际到达的 `stop_accepting()`，随后进入 `draining`。

保持：已接收流程保留 provider 操作与完成通知；本阶段不回收池／传输

</Step>

<Step title="完成已接收任务">

继续执行启用的模块更新，再对 owner runtime 做一次有处理器数量上限的 poll；不取消已接收工作。

保持：已接收任务的后续 await 仍可提交 provider 操作

</Step>

<Step title="退出活跃调用栈">

任务数归零后，仍须等待 update／poll／最终完成回调退出。

保持：最终完成回调执行前，任务已经从组中移除

</Step>

<Step title="检查所有模块">

所有 `ready_to_shut()` 返回 true 后，才能运行任何 `pre_shut()`。

保持：返回 false 则继续等待和 owner 更新；抛出异常则进入不可恢复的 `failed`

</Step>

<Step title="释放资源">

依次遍历全部 `pre_shut()`、全部 `shut()`、插件出口／卸载，最后关闭原生库句柄。

保持：每个钩子返回前，其依赖必须保持有效

</Step>

<Step title="结束">

全部成功，重复 `stop()` 返回 true。

保持：同步输出停机消息，以退出码 `0` 结束

</Step>

</Steps>

<Aside type="caution" title="停止接收顺序不等于完整清理顺序">

`stop_accepting()`、`ready_to_shut()` 默认返回 true，不能完成入口迁移或证明未受管工作结束；没有 `drain_ready()` API。只有停止接收使用反向拓扑。就绪、`pre_shut()`、`shut()`、插件／库释放仍用原顺序；单个插件的注销顺序不能约束其他插件。

</Aside>

### 原生工作与保留规则

| 所有者 | 现有完成／回收路径 | 边界 |
| --- | --- | --- |
| 受管任务 | `task_group::stop_accepting()`、模块服务结果发布、共享 `manual_runtime::poll()` | 任务数和处理器数量上限不代表原生线程已退出，也不代表停机耗时上限 |
| MySQL／Redis | `ready_to_shut()` 观察服务和 bridge 静止；`pre_shut()` 关闭接收入口并回收原生资源；`shut()` 确认 `native_retired()` 后清除连接 | 原生回收调用固定版本 arknet 池的 `close_and_wait()`；旧服务 `stop()` 只请求关闭／抑制回调，不提供同等屏障 |
| Net HTTP 客户端／路由 HTTP 服务端 | 关闭新请求接收，等待请求／回调或路由／响应发送排空，再在 `pre_shut()` 中同步停止池／服务端 | 客户端保留受信任范围内的续接；HTTP 服务端不暂停 TCP accept。独立后台工作须单独托管，正常原生关闭仍可能阻塞 |
| 显式注册的 Net 外部业务根 | [`reg_external_root_callback()`](../reference/network_CN.md#显式注册外部业务根回调) 在所有者线程派发时检查管理器状态，由应用提供拒绝处理 | 普通回调保持原样；管理器及借用接收者须覆盖注册寿命，不提供注销或卸载屏障 |
| Net 服务管理器 | 持有监听器、客户端和回调；当前 `shut()` 没有显式回收这些服务 | 对象析构和已有 DSO 测试覆盖具体释放路径，不构成通用的依赖感知回调屏障 |

通过 `manager.update()` 驱动业务续接；直接调用 `task_group::poll()`／`manual_runtime::poll()` 不授予排空期间的 HTTP 续接权限。原生 IO 保留池线程。回调返回且原生回收屏障完成后，才能销毁所有者或卸载代码。`dlclose`／`FreeLibrary` 成功不证明库映像已从内存移除。

默认无限等待。正数 `--shutdown_timeout_ms` 设置单调时钟 `stop_until()` 截止时间，后续 stop 调用沿用。检查发生在停止接收／就绪钩子返回后、`pre_shut()` 前及清理钩子／关闭调用返回边界；任务归零不能跳过慢钩子的超时保留。

| 结果 | 保留行为 |
| --- | --- |
| 清理前 `timed_out` | 允许 owner 更新；晚完成也不能恢复清理 |
| 清理开始后 `cleanup_timed_out` | 保留剩余资源；停止后续更新与清理 |
| 钩子／关闭失败 `failed` | 停止更新及后续清理 |
| 排空 update 失败 | 即使后续更新恢复，启动器仍保留资源 |

<Aside type="caution" title="截止时间不能抢占阻塞调用">

上述状态不可恢复。检查不能中断 update、钩子或原生关闭，也不能撤回已执行动作。超时不授权强制取消、销毁待完成帧或按成功退出。

</Aside>

## 已选路线与后续实施

| 后续实施边界 | 所需证明 |
| --- | --- |
| 获取前登记独立清理 | owning capsule 与 16 个 owner 的预分配登记；保留首个失败，单独报告清理结果。已进入但未完成的阶段也可持有资源。私有时间轮不覆盖共享 Utility Timer／IO，不提供通用回滚、正常停机、DSO 卸载或同进程重启 |
| 补齐 provider 声明 | `module::get_dependencies()`／`module_base<interface, providers...>` 在 `init()` 前验证键，不验证原始 `add_module()` 条目的动态类型。已迁移 Timers／Utility；其他边须来自实际保留的指针／回调 |
| 完整依赖感知清理 | 先退役回调、定时器登记及跨插件捕获，再卸载代码；保留任务／活跃栈／就绪检查和不可恢复的保留规则。就绪依赖其他模块先清理形成阶段环，应在停止接收时解除，或另行评审阶段变更 |

Provider 清点：Game 保留 Kernel／Guid／Bus／Msg／Net／Map；Net 管理器保留 Bus／Timer；Kernel 保留 Map／元数据／配置／Guid。安装即可获取资源，依赖验证不能替代获取点清理；不猜测依赖或静默改变旧钩子语义。

成功启动后缓存拓扑；手动管理器或合法 idle registry 变化在首个停止接收钩子前重建／验证一次，不在 poll／重入中重读。声明在注册期间保持稳定。完整 `pre_shut()`／`shut()` 顺序仍待后续实施。

本项不涉及 Registry、热更新、服务发现或网络协议。

## 交付阶段

路线已经选定；下表区分实施范围，不再将其列为待确认决策。

| 已选方向 | 当前交付 |
| --- | --- |
| 模块级必需 provider 声明、缺失／环验证与有序启动 | 框架阶段已提供；已迁移 Timers／Utility，无声明模块仍作为独立节点 |
| 独立停止接收，已接收任务继续访问 provider | Core 合同、HTTP 请求级接入、Net 根回调显式注册、访客房间 Join／Move／Action 与受控业务插件已提供；其他应用回调仍须显式分类和迁移 |
| 启动失败时可选清理，随后退出 `1` | 已提供 16 个 owner 的预分配登记、逆序清理与提前停止、最近调用记录及私有独立时间轮验证；共享 Utility Timer、IO 和完整阶段历史仍在本切片之外 |
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

访客房间通过[实际业务入口](../../examples/guest_room/README_CN.md#停止接收业务请求)拒绝新的 Join／Move／Action，使用现有 `server_stopping` 错误并关联 Action 序列号，保持状态不变及既有工作量／输出限制。心跳、断线墓碑和已接纳任务完成继续执行。受控宿主在有观察期限的独立子进程内加载真实 Room／Kernel／Net 动态库，但通过链接保留房间 DSO，不验证库映像卸载，也不提供原生发送交付保证。共享 IO、Registry、热更新、嵌套线格式及 TCP 5000 专项继续暂停。

Core 停止接收测试覆盖缓存反向顺序、一次性／重入、手动 stop、合法 idle 新增／移除、同键替换、无效图，以及元数据／钩子失败。[模块指南](../guides/modules_CN.md#停止接收新业务根请求)记录固定版本 arknet 缺少独立停止接收 API 的边界；stop 服务端或修改 acceptor 都不能安全替代。

独立的 `check-app-shutdown-deadlines` 包含三个慢钩子成功返回场景，不依赖受管任务或原生线程等待：停止接收／就绪钩子超过截止时间后不能进入 `pre_shut()`；慢 `pre_shut()` 返回后不能执行 `shut()`／卸载或后续模块更新。进程持续保留，由测试终止自己创建的子进程。检查覆盖钩子返回边界，不代表抢占或回滚。

永久 Core 依赖测试覆盖 provider 缺失／自依赖／成环、传递／菱形图、重复边、独立节点稳定排序、元数据异常，以及每个启动阶段的首个失败终止。`check-app-module-dependencies` 用真实 Timers DSO 加载缺少 Utility 的应用，要求在 `init()` 前发生依赖失败，并由启动器以退出码 `1` 结束；已有类型化示例／预设测试验证完整应用。本地记录与托管验收继续分开。后续阶段仍须永久测试每个已进入阶段／获取点的补偿、跨插件回调生命周期，以及已接收工作后续调用期间 provider 持续存活；有序启动本身不提供这些停机保证。

阶段 1 之前的源码入口：[管理器](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/core/plugin_manager.hpp)、[启动器](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/application/main.cpp)、[任务组](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/core/task.hpp)、[Net 服务](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/net/net_service_manager_module_impl.cpp)、[MySQL](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/mysql/mysql_module.cpp)、[Redis](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/redis/redis_module.cpp)。原生池语义以配置固定的 [arknet 版本](https://github.com/OpenArkStudio/arknet/tree/a0dfe09cd543e75cfb5996e537ce2fa49f2a758f) 为准，不代表未经验证的依赖升级。
