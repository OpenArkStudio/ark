# 路线图

本页索引已支持的能力与剩余工作。C++20 是构建基线。[历史路线图](roadmap-history_CN.md)保留早期规划，未勾选的设想不代表已经排期。

## 能力与剩余工作

| 领域 | 已提供 · 详细说明 | 剩余工作 |
| --- | --- | --- |
| 项目布局 | 插件源码共置、显式 `<ark/...>` 公开头文件映射 · [布局与接口](../reference/directory-structure_CN.md) | 逐项审查剩余模块与可选集成；C++20 和头文件布局不代表全部现代化改造完成 |
| 数据库 | 所有者线程 Asio 协程接口、拥有型值、MySQL 预处理语句／事务、Redis 命令／流水线 · [MySQL](../reference/mysql_CN.md)、[Redis](../reference/redis_CN.md) | 目标环境的恢复、持久化、取消／故障与 TLS 验收 · [已测操作与未验收范围](../guides/database-validation_CN.md#验收边界) |
| Entity 配置 | 原生 CSV／XLSX／JSON 导出、C++／C# 字段常量、启动时加载 JSON · [工具](../guides/tools_CN.md)、[Kernel 加载](../reference/kernel_CN.md#json-实体定义与业务配置) | 不提供运行中重载、数据库迁移或客户端／服务端分包过滤 |
| 标量同步 | 拥有型捕获、带修订号的发布端／接收端、显式房间 v1 Protobuf／Godot 接入 · [同步](../concepts/entity-sync_CN.md)、[访客房间](../guides/examples_CN.md) | 通用订阅／授权与嵌套表格／容器复制仍是提案 |
| 启动与停机 | 首个失败向上传递、托管任务接收／排空、协作式停机等待 · [启动器](../guides/applications_CN.md)、[任务](../guides/tasks_CN.md) | 部分启动回滚与通用依赖感知停机；任务排空不代表任意后台工作均已结束 |
| 可选插件 | Kafka 构造／导出检查、OSS SDK 骨架、明确不支持 PhysX 3 配置 · [插件边界](../reference/directory-structure_CN.md) | Kafka broker／驱动验收与 OSS 客户端／请求功能 |

数据库真实服务验收记录保留各自测试的源码版本、macOS 环境与操作。访客房间演示经过有界原生检查和本机 Godot 测试。这些证据不构成生产容量或所有部署环境的验收。

## 优先设计

通用 Entity 订阅与嵌套复制，与启动、停服生命周期同级优先讨论。[同步设计边界](../concepts/entity-sync_CN.md)涵盖授权、嵌套身份与顺序、事件和恢复；房间 v1 已固定标量线协议。性能结论需要负载与所有者线程开销的测量证据。

## 暂缓方向

Registry 与业务插件热更新仍暂缓，需要先确定所有权、在途任务、依赖与兼容性设计。将新范围纳入路线图前，应明确 API 或部署、前置条件与验收证据。
