# 路线图

本页索引已支持的能力与剩余工作。C++20 是构建基线。[历史路线图](roadmap-history_CN.md)保留早期规划，未勾选的设想不代表已经排期。

## 能力与剩余工作

| 领域 | 已提供 · 详细说明 | 剩余工作 |
| --- | --- | --- |
| 项目布局 | 插件源码共置、显式 `<ark/...>` 公开头文件映射 · [布局与接口](../reference/directory-structure_CN.md) | C++20 实现与保留的兼容边界见[代码约定](code-style_CN.md#c20-实现约定)；可选集成仍遵守各自支持限制 |
| 数据库 | 所有者线程 Asio 协程接口、拥有型值、MySQL 预处理语句／事务、Redis 命令／流水线 · [MySQL](../reference/mysql_CN.md)、[Redis](../reference/redis_CN.md) | 目标环境的恢复、持久化、取消／故障与 TLS 验收 · [已测操作与未验收范围](../guides/database-validation_CN.md#验收边界) |
| Entity 配置 | 原生 CSV／XLSX／JSON 导出、C++／C# 字段常量、启动时加载 JSON · [工具](../guides/tools_CN.md)、[Kernel 加载](../reference/kernel_CN.md#json-实体定义与业务配置) | 不提供运行中重载、数据库迁移或客户端／服务端分包过滤 |
| Entity 同步 | 拥有型捕获、有界且带修订号的 C++ 发布端／接收端、[规范嵌套 Protobuf 编解码](../reference/entity-protocol_CN.md)、可移植 C# 接收端及房间背包／装备接入 · [同步](../concepts/entity-sync_CN.md)、[访客房间](../guides/examples_CN.md) | 通用订阅、动态授权、AOI／兴趣管理及原地重置；当前每条流保持固定投影 |
| 启动与停机 | 必需提供方图与有序启动、受管任务取消、消费者优先的逐模块清理与对象析构、全部 DSO 最后关闭，以及最多 16 个 owner 的可选逆序启动清理 · [生命周期状态](lifecycle-closeout_CN.md)、[启动器](../guides/applications_CN.md)、[任务](../guides/tasks_CN.md) | 应用／运维在停止进程前完成公告、关入口、踢人和保存；[受保护的资源切片](startup-failure-cleanup_CN.md#其他-io-边界)不提供通用活跃 IO 退役、业务回滚或未声明外部回调生命周期的保证 |
| 归档插件 | Consul、Etcd Client、Kafka、OSS SDK：保留源码、公开头、目标、显式启用和兼容性检查；运行时插件默认关闭 · [归档集成](../reference/directory-structure_CN.md#归档集成) | 暂停功能开发，未经用户明确重新启用不继续改造 |
| 可选物理集成 | 明确不支持 PhysX 3 配置 · [插件边界](../reference/directory-structure_CN.md) | 暂无已验证的 PhysX 3 集成 |

数据库真实服务验收记录保留各自测试的源码版本、macOS 环境与操作。原生房间及可移植 C# 检查覆盖内置集成；实际 Godot 执行和部署验收须保留各自匹配版本及结果。这些检查不构成生产容量或所有环境的验收。

## 当前优先级

当前工作仅面向本机。[Entity 复制契约](entity-replication-design_CN.md)覆盖拥有型捕获、可信状态、嵌套线协议转换及可移植客户端；房间只保留一套当前协议，要求协调升级服务端和客户端。通用订阅／授权编排仍为 TODO。性能结论需要负载与所属线程开销的测量证据。

| 优先级／前置条件 | 当前决定 |
| --- | --- |
| 环境 | 只有本机在范围内，真实部署暂缓 |
| 发布 | 发布范围与验收标准待定 |
| 大规模及持续验收 | 先解决本机 5000 连接问题并通过其验收；此前不推进更大规模或持续稳定性工作 |
| 调查 | 唯一跟踪入口为 [arknet issue #61](https://github.com/OpenArkStudio/arknet/issues/61)，调查仍暂停 |

## 暂缓方向

Registry 与业务插件热更新仍暂缓，需要先确定所有权、在途任务、依赖与兼容性设计。将新范围纳入路线图前，应明确 API 或部署、前置条件与验收证据。
