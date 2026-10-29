# 可选启动失败清理

启动失败默认立即退出 `1`；只有通过 `plugin_manager::prepare_startup_cleanup()` 预登记的独立自有资源，可先执行一次失败清理。首个切片固定一个槽位，以无业务回调的私有时间轮验证，不接入共享 Utility Timer 或 IO。原有获取点源码基准：[d9618a66](https://github.com/OpenArkStudio/ark/tree/d9618a663fc2069b734007812590c88736d17ed5)。不做同进程重试，不调用正常停服充当回滚，不推断外部业务／事务回滚；Registry 和热更新继续暂停。

## 启动失败时已经存在什么

[加载器](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/core/plugin_manager.hpp#L635)在调用入口前保留已加载 DSO；[插件登记](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/core/plugin_manager.hpp#L372)在 `install()` **之前**释放局部插件候选所有权，安装返回后才写入成功插件表。因此，成功插件表不能作为完整的失败清理清单。

| 失败点 | 实际所有权／已有资源 |
| --- | --- |
| 插件构造抛异常 | DSO 已加载，没有完整插件指针返回；C++ 只析构已完成的基类／成员，不调用未完成对象的析构函数。已逃逸的回调／线程仍需独立所有权证明。 |
| 模块工厂或登记抛异常 | [私有工厂](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/utility/timer_module_impl.cpp)返回 `unique_ptr<interface>`，[类型化登记](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/core/plugin_base.hpp#L54)在记账成功前持有它。拒绝时销毁完整的未发布候选；构造失败时展开已完成子对象。此前已登记模块继续存活；有析构函数不等于非阻塞回收。 |
| 部分安装后抛异常，或安装成功后的插件表插入分配失败 | 已释放的插件及其模块／update 条目可能存在，但成功插件表尚无记录；不会自动卸载或回滚。 |
| 依赖验证失败 | 构造／安装资源已存在；尚未执行模块 `init()`。 |
| `init()` 返回 false／抛异常 | 全部插件已安装；此前模块的初始化资源及当前钩子的部分资源可能存在。构造时启动的 IO 已可能运行。 |
| `post_init()` 返回 false／抛异常 | 全部 init 已通过；此前 post-init 的回调、定时器、已排队工作及当前获取的资源可能存在。 |
| `check_config()` 返回 false／抛异常 | init／post-init 已通过。函数名称不保证只读；此前资源和本次部分获取继续存在。 |
| `pre_update()` 返回 false／抛异常 | 前三个阶段已通过，仍可能留下部分准备。启动器的普通更新循环和 Windows 命令线程尚未开始。 |

四阶段[驱动](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/core/plugin_manager.hpp#L117)在首个 false／异常处停止。[启动器](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/application/main.cpp#L309)刷新诊断后调用 `_Exit(1)`，Windows 还调用 `TerminateProcess`，绕过正常对象／静态析构。这是进程失败保留策略，不证明回调停止或原生线程已回收。

| 真实获取点 | 源码含义 |
| --- | --- |
| [Utility Timer init](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/utility/timer_module_impl.cpp#L48)创建并初始化时间轮；Timers 的 [post_init](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/examples/timers/timers_module.cpp)登记回调 | [时间轮初始化](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/core/time/timer.hpp#L79)先用局部所有者构建，再提交。清空时间轮释放自有存储，不等于模块停机契约。 |
| [HTTP 客户端构造](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/net/http_client_module_impl.cpp#L244)启动 IO 池 | install 期间、任何 init 之前就可能有工作线程；其析构／正常停机调用同步 runtime stop。 |
| Net [post_init](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/net/net_service_manager_module_impl.cpp#L111)排队配置客户端并登记健康定时器；[update](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/net/net_client_service_impl.cpp#L248)才启动排队客户端 | 排队不等于连接完成。[create_server](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/net/net_service_manager_module_impl.cpp#L282)可在调用它的启动钩子中获取监听器，须记录真实调用点，不能假定统一阶段。 |
| HTTP [端点预约](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/http/http_server_module_impl.cpp#L111)先于原生构造／启动 | 后续异常会使预约缺少所有者；返回 false 分支也在释放前复制字符串。独立异常安全候选见下文。 |
| MySQL [构造／init／configure](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/mysql/mysql_module.cpp#L218) | 构造持有状态，init 绑定所属 runtime；`configure()` 在写入具名连接表**之前**构造原生池，后续插入抛异常时线程可能已启动。固定版 arknet `a0dfe09c` 在池构造中启动 IO。 |

## 独立资源登记与执行

所属线程在资源获取前创建完整的 owning capsule，并调用：

```cpp
auto capsule = std::make_shared<owned_timer_context>();
if (!manager.prepare_startup_cleanup(
        "isolated-wheel", "my-plugin", capsule, release_timer_context)) {
    throw std::runtime_error("startup cleanup enrollment rejected");
}
return std::make_unique<isolated_timer_module>(capsule);
```

API 接收 owner 键、代码所有者键、强持有的 `shared_ptr<void>` 和普通函数指针 `startup_cleanup_result (*)(void*)`。两个键均为非空、无 NUL、最多 95 字节；固定容量为 **1**，满槽、无效输入、错误线程、重入执行器或已结束启动均返回 false，登记本身 `noexcept` 且不分配。失败后不得获取本次资源。上下文分配失败发生在获取之前，走默认退出。

允许启动前 idle 或启动／安装阶段预登记，但只在一次 `start()` 已返回 false 或抛异常并展开后，才可执行失败清理。启动尝试结束后不能再次 start。同进程嵌入调用者必须在清理后退出 `1`；清理函数不会提供重试或恢复入口。启动前、尚未进入 start 的独立构造异常仍采用原有 RAII／默认退出。

构造函数保存 capsule，通过无异常移动把时间轮句柄交给它，再初始化。未完成模块的析构不参与清理；宿主在构造抛异常后仍持有 capsule。动作检查缺席／部分时间轮，调用**自有资源** `time_wheel_manager::shut()` 后清空句柄，返回 `done`。不调用模块 `pre_shut()`／`shut()`，不触发业务回调。永久测试的私有 wheel 不启动线程、不向外发布，也不借用 provider；它不授权共享 Utility Timer 的消费者清理。

![启动失败清理与保留失败退出](../assets/startup-failure-cleanup_CN.svg)

| 记录 | 有界范围与含义 |
| --- | --- |
| `get_startup_call()` | 仅保留最近进入的受宿主管理调用：插件构造／install、加载／依赖检查、四个模块钩子。owner 95 字节、phase 23 字节固定缓冲，调用前记录进入，返回后记录通过／false，异常时记录 exception；不是全部对象的阶段历史。依赖检查不会据此推断资源。 |
| 自有资源槽 | 获取前固定键、strong capsule、代码所有者、action 进入标记及结果。由 capsule 表达真实部分获取；阶段和缺少 acquired 标记都不证明资源不存在。 |

启动器先保留主失败诊断，再在外层 start 已返回／展开后调用 `cleanup_failed_startup()`。管理器封闭新登记及 managed task root admission，不 cancel、不 poll；保护生命周期／registry 重入。动作最多调用一次，`unsafe`／`failed`／异常均保持失败；异常由宿主捕获。重复调用返回缓存结果，既不重复释放，也不调用正常 stop、uninstall 或 DSO close。成功启动只解除尝试登记，完整模块必须继续持有 capsule。

清理后无论结果如何都退出 `1`。全部已加载代码、未回收所有者及 capsule／控制块析构代码保留到进程退出；即使动作返回 done，也不在失败路径销毁 capsule。普通未登记模块、部分安装但未进入成功插件表的对象和原有构造展开保持既有行为。动作必须独立、有限且不会阻塞；宿主不能抢占一个阻塞函数。

紧急诊断使用 1024 字节栈缓冲和原生 stderr 写入，保留 phase／owner 优先于异常详情，超长文本截断；不构造字符串、格式化日志或调用虚命名方法。若分配故障打断普通阶段诊断构建，使用固定调用记录。Windows 继续 `TerminateProcess`，其余平台继续 `_Exit(1)`，绕过正常析构。原生写失败也继续退出。

**兼容性：** 不增加 module 虚方法，不改变正常停服顺序；`plugin_manager` 新增内联状态，改变公开类布局，宿主和全部插件／消费方须使用同一头文件重新构建。不得混用旧二进制。

**独立 HTTP 候选，不等于框架失败清理：** 预约成功后，构造／绑定／启动或后续 `listen_key_ = endpoint` 复制抛异常，可使 `http_endpoints` 残留预约。可用 cpp 私有的局部预约所有者在 false／异常时只释放自己持有的键，原生启动成功后用无异常字符串 swap 转交。无需公开注入 API，也不改变生命周期。独立分配故障测试可在单独翻译单元编译生产 cpp，在内部集合已包含该键后的首个分配点、工作线程创建前注入失败，随后要求同端点可复用；保留既有重复所有者／重启检查，并证明最终提交不分配字符串。这里只给据源码推导的复现计划，未执行。

## IO 不进入首个切片

Net／HTTP／MySQL 接入前，先证明构造／成员展开不会丢失活跃所有者，也不会在失败清理入口前阻塞。线程创建前就须有独立上下文，回调持有完整状态，不能依赖未完成模块的 `this`。回调代码、接收者、bridge／runtime 和原生所有者须存活到可证明的回收屏障完成。普通模块 update 可能运行部分启动的业务代码，不能作为清理 pump。

固定版 arknet 的 `io_pool::stop()` 可能等待／join；MySQL 的[回收](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/mysql/detail/mysql_service.hpp#L432)使用 `close_and_wait()`，服务 `stop()` 仅请求关闭／抑制回调。见[固定版原生池](https://github.com/OpenArkStudio/arknet/blob/a0dfe09cd543e75cfb5996e537ce2fa49f2a758f/include/arknet/mysql/pool.hpp#L972)。这些不是非阻塞启动失败清理契约。登记前必须有现成或单独评审的「请求／已回收」观察、有界宿主驱动及回调策略；不能用分离线程隐藏阻塞关闭，也不在这里增加 promise 或补偿事务。关闭本地池不证明 SQL／业务回滚，也不改写未知原生结果。

## 有限验收

`cmake --build <build> --target check-startup-cleanup --parallel 1` 运行常驻 C++ 测试：四阶段 false／异常、单槽拒绝／错误线程、成功解除登记、unsafe／异常／重复清理与重入；真实 launcher 加载独立 DSO，覆盖获取前／部分／完成后的构造异常、部分安装异常及四阶段失败，检查释放一次、exit1、没有后续 update／正常停服／uninstall／DSO 或 capsule 析构。隔离分配故障子进程保持 global new 持续失败直到真实紧急退出，验证 capsule 分配失败、无分配登记／记录、阶段诊断失败及部分 wheel 初始化失败。原有未登记真实监听器场景由 `check-app-startup-failures` 保留。

启动分配故障注入前先准备好调用参数。另设调用前失败用例，验证尚未进入 `start()` 时保留已登记资源，不执行清理。

这些是本机有限模块／私有资源验收，不证明 IO 回收、业务回滚或托管平台通过；原始日志留在本地 receipt。HTTP 异常候选仍未修复或执行复现。

以下两项 IO 决策保留 TODO，不阻塞私有时间轮切片：

1. 哪个 IO 所有者首先接入；它已接收的启动操作中，哪些必须完成、哪些允许取消？源码不能替业务选择完成策略。
2. 该部署允许启动失败清理等待多少绝对时间？超时仍保留所有者并退出 `1`；具体数值须来自运维要求，且不能抢占阻塞 API。
