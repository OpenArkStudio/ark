# 路线图

本页索引已支持的能力与剩余工作。C++20 是构建基线。[历史路线图](roadmap-history_CN.md)保留早期规划，未勾选的设想不代表已经排期。

## 能力与剩余工作

| 领域 | 已提供 · 详细说明 | 剩余工作 |
| --- | --- | --- |
| 项目布局 | 插件源码共置、显式 `<ark/...>` 公开头文件映射 · [布局与接口](../reference/directory-structure_CN.md) | 逐项审查剩余模块与可选集成；C++20 和头文件布局不代表全部现代化改造完成 |
| 数据库 | 所有者线程 Asio 协程接口、拥有型值、MySQL 预处理语句／事务、Redis 命令／流水线 · [MySQL](../reference/mysql_CN.md)、[Redis](../reference/redis_CN.md) | 目标环境的恢复、持久化、取消／故障与 TLS 验收 · [已测操作与未验收范围](../guides/database-validation_CN.md#验收边界) |
| Entity 配置 | 原生 CSV／XLSX／JSON 导出、C++／C# 字段常量、启动时加载 JSON · [工具](../guides/tools_CN.md)、[Kernel 加载](../reference/kernel_CN.md#json-实体定义与业务配置) | 不提供运行中重载、数据库迁移或客户端／服务端分包过滤 |
| Entity 同步 | 拥有型标量／嵌套捕获、有界且带修订号的 C++ 发布端／接收端、房间标量 v1 Protobuf／Godot 接入 · [同步](../concepts/entity-sync_CN.md)、[访客房间](../guides/examples_CN.md) | 通用订阅／授权、嵌套线协议编解码及客户端适配；嵌套 C++ 状态不改变房间 v1 |
| 启动与停机 | 必需 provider 依赖图与有序启动、消费者优先的 `stop_accepting()`、HTTP 请求级停止接收／排空、托管任务排空、协作式停机等待，以及私有独立时间轮的可选单槽清理 · [生命周期状态](lifecycle-closeout_CN.md)、[启动器](../guides/applications_CN.md)、[任务](../guides/tasks_CN.md) | 剩余 Net 协议入口迁移、完整依赖感知清理与共享 IO 启动失败清理；任务排空不能证明任意后台工作已结束，也不提供通用回滚 |
| 可选插件 | Kafka 构造／导出检查、OSS SDK 骨架、明确不支持 PhysX 3 配置 · [插件边界](../reference/directory-structure_CN.md) | Kafka broker／驱动验收与 OSS 客户端／请求功能 |

数据库真实服务验收记录保留各自测试的源码版本、macOS 环境与操作。访客房间演示经过有界原生检查和本机 Godot 测试。这些证据不构成生产容量或所有部署环境的验收。

## 优先设计

当前优先推进官方模块停止接收、启动失败清理与文档同步。通用 Entity 订阅和嵌套线协议／客户端适配暂缓，后续另行讨论。[同步设计边界](../concepts/entity-sync_CN.md)涵盖授权、嵌套身份与顺序、事件和恢复。可信绑定的嵌套 C++ 基线／增量／接收端已提供，房间 v1 保留标量线协议。性能结论需要负载与所有者线程开销的测量证据。

## 暂缓方向

Registry 与业务插件热更新仍暂缓，需要先确定所有权、在途任务、依赖与兼容性设计。将新范围纳入路线图前，应明确 API 或部署、前置条件与验收证据。
