# 官方插件

[English](README.md)

全部官方插件的头文件、实现与装配均共置于 `src/plugins/<名称>/`。CMake 使用显式公开头映射保留 `<ark/<名称>/...>` 逻辑路径，关闭运行时插件后仍可使用。Kernel、Consul、Etcd Client、Kafka、OSS SDK、Physic 保留原有全部公开头，包括实现头与装配头；此前 MySQL、Redis、Utility、Net、Bus、HTTP 的映射仍排除私有装配与工厂。每个插件继续按独立的可选 CMake target 构建，业务插件放在应用仓库。

| 插件 | 职责 | 默认构建 |
| --- | --- | --- |
| kernel | 实体数据、配置和事件服务 | 开启 |
| net | 通过 arknet 提供 TCP、RUDP、WebSocket 和 HTTP 服务 | 开启 |
| bus | 进程关系和消息路由 | 开启 |
| utility | ID、定时器、调度和压缩 | 开启 |
| etcd_client | etcd 服务访问 | 开启 |
| [http](../../docs/reference/http_CN.md) | 带路由的 HTTP 服务端模块 | 关闭 |
| consul | Consul 服务发现 | 关闭 |
| kafka | Kafka 消息服务 | 关闭 |
| osssdk | OSS SDK 骨架 | 关闭 |
| physic | 物理服务 | 关闭 |
| mysql、redis | arknet 原生数据库连接池与公开协程接口 | 关闭 |

使用 `ARK_BUILD_PLUGIN_<名称大写>` 选择插件。可选插件需要各自的依赖和验证。[MySQL](../../docs/reference/mysql_CN.md) 提供查询、预处理语句和事务；[Redis](../../docs/reference/redis_CN.md) 提供命令和有序流水线。两者均通过具名配置和管理器托管的协程任务使用。构建与运行时选择见[数据库插件启用方式](../../docs/reference/build-options_CN.md#原生数据库插件)。Lua、Actor 的整合仍在规划中。

CMake target 和构建命令见[项目布局与公共插件接口](../../docs/reference/directory-structure_CN.md)。

[Utility 参考](../../docs/reference/utility_CN.md)说明四个模块接口、所属线程驱动的定时器与实现头迁移。

Kernel 的原有 38 个头与 cpp 共置于 `src/plugins/kernel/`，通过 `ark::kernel` 保留全部 `<ark/kernel/...>` 路径，包括 `data_impl.hpp`、`data_list_impl.hpp` 和 `kernel_plugin.hpp`。保留的公开实现类型与插件类型使用类型化构造、私有拥有型工厂和显式入口。`kernel_factory.hpp` 不映射、不安装。公开类布局发生变化，Kernel 与消费者必须一起重建；注册键、update 选择和生命周期顺序保持不变。兼容边界、消费检查及 Protobuf 条件见 [Kernel 构造迁移](../../docs/reference/kernel_CN.md#kernel-类型化构造)。

Consul 保留原有三个公开头及实现／插件类型，同时使用类型化构造、私有拥有型工厂和显式入口。公开类布局发生变化，Consul 与消费者必须一起重建。唯一接口注册键、禁用 update 与请求行为保持不变。构建条件及无需服务的构造检查见 [Consul 构造迁移](../../docs/reference/directory-structure_CN.md#consul-类型化构造)。

其他四类旧插件保留原有 19 个头的全部字节。映射检查不代表 Consul、Kafka、OSS SDK 的运行时集成已经验证。Physic 保留未受支持的 PhysX 3 配置失败；参见[可选旧集成依赖](../../docs/dependencies_CN.md)。
