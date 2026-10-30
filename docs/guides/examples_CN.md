# 按能力命名的示例

内置示例使用能力名称。构建时开启 applications 和 examples；此次命名调整不改变启用集合或 update 参与方式。

| 目录／应用名 | 插件库名 | 用途与支持状态 |
| --- | --- | --- |
| `examples/timers`／`timers` | `timers_plugin` | Utility 定时器与模块生命周期；启用，参与 update |
| `examples/module_lifecycle`／`module_lifecycle` | `module_lifecycle_plugin` | 生命周期与少量 data-list 演示；启用，无 update 覆写。回调演示代码仍被注释 |
| `examples/entity_operations`／`entity_operations` | `entity_operations_plugin` | Kernel 实体、数据和表操作；启用，无 update 覆写。启动仅做有界功能演示；[旧计时负载](../../benchmarks/ENTITY_OPERATIONS_CN.md)只能通过显式基准命令运行 |
| `examples/guest_room`／`guest_room` | `guest_room_plugin` | 可选二维访客大厅，使用 JSON 定义的 Kernel Entity 字段／配置、权威移动与 HP／MP 操作、Protobuf 标量基线／增量，以及真实 Net 传输；通过 `ARK_BUILD_GUEST_ROOM_EXAMPLE` 启用。[构建、ARK2 协议与原生验证](../../examples/guest_room/README_CN.md)；官方 [C# Godot 客户端](https://github.com/OpenArkStudio/ark-client-godot) |
| `examples/mysql_sample`／`mysql_sample` | `mysql_sample_plugin` | 原生 MySQL 协程示例；应用示例列表中仍关闭，开启 SSL 的数据库测试检查其装配 |
| `examples/redis_sample`／`redis_sample` | `redis_sample_plugin` | 原生 Redis 协程示例；应用示例列表中仍关闭，数据库测试检查其装配 |
| `examples/physics_scene`／`physics_scene` | `physics_scene_plugin` | 关闭，依赖不支持的旧 PhysX 3；保留源码与清单不代表集成可运行 |
| `examples/http_server`／`http_server` | `http_server_plugin` | 默认应用示例中关闭；支持[类型化创建与受控动态库回归](../reference/http_CN.md#旧-http-示例的类型化创建) |
| `examples/http_client`／`http_client_sample` | `http_client_sample_plugin` | 默认应用示例中关闭；使用固定外部端点演示 future／回调 POST 与 etcd 租约 |

每个目录中的 hpp/cpp 继续并排存放。可从 [timers 快速开始](../get-started/quick-start_CN.md)入手。
通用启动脚本选择 `conf/<name>.app.toml`，因此进程名称和应用清单都应使用能力名称。Kernel 示例仍需要业务元数据。

## 访客房间停止接收

访客房间将 Join、Move、Action 显式注册为外部业务根。管理器离开 `running` 后，在回调入口返回 `server_stopping`；Action 携带关联序列号，拒绝不修改成员关系或 Entity 状态，并沿用既有工作量／输出限制。心跳、连接事件和断线墓碑继续派发，已接纳工作可以完成。真实 Room／Kernel／Net 的受控原生测试在独立、有观察期限的子进程中覆盖此行为；不提供原生发送、卸载或 Godot 验收保证。见[示例验证范围](../../examples/guest_room/README_CN.md#原生验证)。

## 迁移编号示例名称

| 原目录、应用名和标识符前缀 | 新前缀 |
| --- | --- |
| `sample1` | `timers` |
| `sample2` | `module_lifecycle` |
| `sample3` | `entity_operations` |
| `sample4` | `physics_scene` |
| `sample5` | `http_server` |

模块和消费者必须一起重新构建：C++ 模块／插件类型名和 RTTI 注册键均会变化。
将 `sample1_plugin` 等旧库名替换为 `timers_plugin`，保留对应平台扩展名和 Debug `_d`。
将已部署的 `conf/sample1.app.toml` 换为 `conf/timers.app.toml`，并按上表逐项同步 `--name`、`--app_conf`、启动脚本参数和部署 ConfigMap。
不再生成旧 target、旧库别名或旧配置文件名。已有运行目录不会自动迁移；切换前应重新准备同次构建的可执行文件、插件集与配置。

按字节保存的历史文档归档仍保留原始名称和命令，不作为当前命名指南。本页说明名称与支持边界；HTTP 示例测试使用公开服务替身，不代表外部服务或性能验收。

## 运行实体操作示例

在独立运行目录准备同次构建的产物、`config/entity_operations.app.toml`、`config/kernel.plugin.conf`，并复制 `examples/entity_operations/resource`。使用 `--name=entity_operations --app_conf=conf/entity_operations.app.toml` 启动，详见[示例说明](../../examples/entity_operations/README_CN.md)。启动创建四个玩家并演示数据、表和自定义值，不执行计时循环；真实 DSO 回归使用相同清单与元数据。独立基准必须显式指定 `--iterations N`；八次迭代的 smoke 关闭计时，不构成性能证据。

## 数据库示例装配

MySQL／Redis 示例使用 `module_base<module>`、`plugin_base<derived_type>`、插件私有的拥有型工厂，以及显式 `DllEntryPlugin`／`DllExitPlugin` 入口。两者均保留具体示例模块的 RTTI 注册键、继承的默认 `update()` 和 `module_update::disabled` 注册。hpp/cpp 继续共置，协程操作、连接名称与配置不变，不启用其他保留的旧示例。

[数据库回归](../../tests/database/README_CN.md#示例插件装配)将模块与插件源文件一起编译为真实 DSO，检查入口、注册、数据库接口查找和退出。本地 provider 在配置阶段返回 false，不启动协程或网络操作。这些装配检查不运行示例业务流程，也不代表真实服务部署已经验收。
