# 服务器架构

ark 可以使用同一组插件与模块接口组合不同的服务进程。下图用于为多进程部署分配职责，不表示已经提供开箱即用的集群、Registry 服务或实测容量。

![参考进程职责与部署边界](../assets/server-topology_CN.svg)

## 先划分职责，再扩展规模

| 参考角色 | 职责 | 应用需要决定 |
| --- | --- | --- |
| 入口与目录 | 将客户端引导到环境或入口端点 | 鉴权、端点选择与发现来源 |
| 集群服务 | 协调跨区共享工作，例如 Master 或公共业务服务 | 状态归属、恢复与故障切换 |
| 路由进程 | 在区服与集群进程之间转发指定消息 | 目标标识、允许的路由与拥塞处理 |
| 区服服务 | 承担 World、Game、Proxy、Login 等职责 | 分片、玩家分配与区服边界 |
| 存储服务 | 独立于进程路由保存应用记录 | 数据模型、复制、持久性与备份 |

并非每个应用都需要为这些角色单独部署进程。先保留负载需要的进程，当状态归属、扩容或故障行为需要分离时，再增加边界。插件边界本身不等于网络边界或高可用边界。

## 仓库提供的内容

`src/server/` 包含 `master`、`router`、`world`、`game`、`proxy`、`login`、`dir` 等示例角色，`config/*.app.toml` 中的相应清单选择插件。角色目录与配置展示组合方式，不代表参考拓扑中的每种服务都已经完整实现。

Bus 插件表示进程地址与路由目标。[Bus 参考](../reference/bus_CN.md)说明如何配置本地进程关系、创建连接，以及携带所需标识发送消息；[网络插件](../reference/network_CN.md)提供传输集成。进程内的模块调用仍使用直接接口，见[应用架构](application-architecture_CN.md)。

## 从已验证的进程对开始

1. 按[快速开始](../get-started/quick-start_CN.md)准备一个应用的运行目录。
2. 为每个进程分别设置 Bus 标识、清单与端点。动态库文件名不是进程标识。
3. 先验证本机客户端与服务端或 Bus 连接，用小负载检查两端日志，再增加路由层。
4. 增加多个区服或 Router 前，明确状态归属与恢复规则，并将目标负载和故障测试保存在部署配置旁。

服务发现是独立的集成选型，框架并不要求每种部署都启动 Consul。配置文件端点、平台发现与可选集成各有运维要求；它们都不表示框架已经提供自研多节点 Registry 或自动故障切换。[路由设计提案](../project/network-design_CN.md)描述历史上的 cluster/router/zone 思路，不是已发布的高可用契约。

## 部署时需要验证的边界

端点可连通，不等于已经验证鉴权、重连后的消息交付、状态转移、网络分区行为或存储一致性。10 万至 20 万同时在线是多进程部署目标，不是实测的单进程上限。[基准测试与验收](../performance/benchmark_CN.md)区分本机正确性与性能工具，以及跨主机容量和耐久测试。

## 验证服务端创建方式

内置服务端插件使用类型化模块工厂与显式动态库入口。改造保留插件版本、配置访问、接口 RTTI 注册名、安装顺序与关闭帧更新的行为；网络回调、生命周期方法、协议与资源行为保持不变。

安装[应用构建依赖](../networking_CN.md)后运行：

```sh
cmake -S . -B out/server-construction -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  '-DVCPKG_MANIFEST_FEATURES=application;boost;etcd-client' \
  -DCMAKE_BUILD_TYPE=Debug -DASIO_STANDALONE=OFF \
  -DARK_BUILD_APPLICATIONS=ON -DARK_BUILD_PROTO=ON \
  -DARK_BUILD_CORE_TESTS=ON -DARK_BUILD_EXAMPLES=OFF
cmake --build out/server-construction --target check-server-plugins --parallel 2
```

`check-server-plugins` 对七个启用的服务动态库分别检查真实工厂与装配、原有源码包含路径和关闭更新的注册方式，并加载实际动态库检查两个导出入口符号。每个角色先在仅链接核心的最小进程中检查导出，再在独立进程中检查真实工厂，避免宿主与动态库在同一进程内重复注册静态链接的 AFProto 描述符。真实 Login 装配另以计数替身检查被拒绝候选对象的析构、原有注册的保留以及重试成功。应用构建中的 `check-core` 包含这些定向检查。另一个动态库夹具通过管理器加载器运行真实 Login 入口与退出函数，并使用受控模块。动态库导出检查进程不调用业务生命周期。真实工厂进程还使用受控依赖检查下文所述的启动结果传播。工厂与导出检查不启动网络服务，也不代表部署就绪；下文的独立启动器检查覆盖本地监听器。

`node --test tests/ci/server-construction.test.mjs` 在不编译原生代码的情况下检查兼容转发映射与创建约束。Simulate 只参与源码策略检查，其未受支持的 PhysX 3／MessagePack 集成不在这些原生目标中。

### 角色启动错误

Login、Proxy、Router、Master、World 和 Dir 的网络启动路径先检查 Bus 与 Net。Game 还会在注册现有回调前检查 Kernel。缺少启动必需的模块时，`init()` 返回 `false`；创建监听器失败或找不到服务端网络服务时，`post_init()` 返回 `false`。管理器保留失败的角色与生命周期阶段并停止启动，启动器输出诊断后以退出码 1 结束。监听器设置成功时返回 `true`。这些路径不再以退出码 0 终止进程。本次变更不定义回滚或部分启动后的清理顺序。

真实工厂测试还会通过公共角色装配和管理器运行受控的 Bus/Net 场景：缺少依赖、创建失败、服务缺失，以及六条仅设置监听器的启动路径成功。Game 覆盖缺少依赖的场景，包括缺少 Kernel；测试不模拟 Kernel，也不声称 Game 生命周期正常。在 macOS 和 Linux 上，`check-server-startup` 另以独立 Bus 夹具运行六个角色各自的真实 Utility、Bus、Net 和角色动态库，检查健康回环启动，以及测试持有的端口被占用时的失败角色、阶段与启动器退出码 1。该夹具不验收仓库内的角色预设或业务处理器。

未启用的 Simulate 源码采用相同的失败传播方式，并在注册回调前检查模拟模块。其 PhysX 3/MessagePack 集成仍不受当前构建支持，因此没有本套测试的原生运行验收。依赖检查只覆盖现有启动代码实际解引用的模块，不能证明尚未完成的角色操作已经就绪。

```sh
cmake --build out/server-construction --target check-server-startup --parallel 1
```

## 验证本地启动器组合

在 macOS 和 Linux 上，`check-server-launcher` 使用真实 `app` 可执行文件以及 Utility、Bus、Net 和 Login 动态库。测试创建独立运行目录，为 Bus 身份 `1.1.32.1` 配置临时回环端口，等待 Login 监听器就绪后建立一个 TCP 连接，再向测试创建的进程发送 `SIGTERM`。测试检查服务器 EOF、新连接被拒绝，以及进程以退出码 0 结束。客户端收到服务器的 FIN 后，先关闭自身发送侧，再等待进程退出。原始 TCP 客户端若一直保持连接，传输层可能等待现有的优雅断开超时；此等待与辅助程序五秒的配合退出上限不同。启动和退出等待均有上限，辅助程序只回收自己创建的子进程。配置和日志保留在构建目录中，每次运行使用新目录。

```sh
cmake --build out/server-construction --target check-server-launcher --parallel 1
```

配置应用构建时需启用 `ARK_BUILD_PLUGIN_UTILITY`、`ARK_BUILD_PLUGIN_BUS` 和 `ARK_BUILD_PLUGIN_NET`。测试生成最小清单，不修改随仓库提供的角色预设。Bus 配置格式仍要求 `registry` 表；此本地组合不加载 Consul，也不注册服务或执行发现。Windows 保留前述构造测试；进程和信号控制测试仅在 Unix 上启用。

此测试验收启动器和传输边界。Login 当前只创建监听器，没有注册账号登录处理器。Proxy 的服务器选择和消息转发回调尚不完整。Router 清单未选择 Router 角色动态库，World 预设的插件路径和配置为空；将这些预设用于部署前，需要应用提供经过审阅的清单。连接成功不能证明认证、访客或会话流程、集群路由、重连恢复或持久化已经可用。

验证现有的有限业务操作时，可启用示例及 Kernel、Utility 后运行 `check-typed-examples`。其中真实 EntityOperations 动态库使用仓库内的清单和元数据，创建并检查玩家数据、表格和自定义值，再于退出时销毁实体。此进程内业务示例与独立的 Login 传输组合分别验证不同边界，两者尚未组成网络游戏登录流程。
