# 官方插件与仓库组织

核心源码与构建定义位于 `src/core/`，启动器位于 `src/application/`；应用清单与共享配置读取位于 `src/config/`。所有官方插件将公开头、实现和私有装配共同放在 `src/plugins/<名称>/`，通过显式 CMake 映射保留逻辑包含路径。业务插件放在应用仓库，`arknet` 保持独立；该布局不改变插件生命周期或热更新能力。

## 现有实现

| 独立仓库 | ark 内的相关实现 | 建议合入位置 | 需要整理的内容 |
| --- | --- | --- | --- |
| [MySQLPlugin](https://github.com/OpenArkStudio/MySQLPlugin) | `src/plugins/mysql` | `src/plugins/mysql` | [原生协程接口](../reference/mysql_CN.md)、预处理语句、事务和共享业务执行器；部署仍需真实 MySQL 验收 |
| [RedisPlugin](https://github.com/OpenArkStudio/RedisPlugin) | `src/plugins/redis` | `src/plugins/redis` | [原生协程接口](../reference/redis_CN.md)、具名连接池、RESP/TLS 和正常停机；部署仍需真实 Redis 验收 |
| [URLPlugin](https://github.com/OpenArkStudio/URLPlugin) | Net HTTP client 与 HTTP server 插件 | `src/plugins/http` | 对照 libcurl 版本的请求、编码与 actor 调用能力，统一 HTTP 服务接口 |
| [LuaScriptPlugin](https://github.com/OpenArkStudio/LuaScriptPlugin) | 需要按新插件接口接入 | `src/plugins/lua` | 整理 Lua 绑定与脚本生命周期，移除对旧 NFComm 接口的依赖 |
| [ActorPlugin](https://github.com/OpenArkStudio/ActorPlugin) | 需要按新插件接口接入 | `src/plugins/actor` | 明确任务调度、消息投递和业务线程的关系 |

这些仓库仍保留不同代际的接口和构建方式。合入时逐项对照功能、重叠实现与依赖。MySQL、Redis 客户端归入 arknet，其他插件的依赖按各自需求讨论。

## 目录布局

<FileTree>

- ark/
  - src/
    - core/ — 核心源码与 target 定义
      - ...
    - config/ — 应用清单与共享文件／TOML 读取
      - ...
    - application/ — 应用启动器
      - ...
    - plugins/ — 官方插件实现与 CMake 文件
      - kernel/
        - ...
      - net/ — 公开头、传输实现与私有创建
        - ...
      - bus/ — 公开模块接口与私有创建
        - ...
      - utility/
        - ...
      - http/ — 公开路由接口与私有创建
        - ...
      - mysql/
        - ...
      - redis/
        - ...
    - proto/
      - ...
    - server/
      - ...
  - examples/ — 示例应用与业务插件
    - ...
  - tests/ — 核心与跨插件集成测试
    - ...
  - benchmarks/ — 可独立编译运行的基准程序
    - ...
  - tools/ — 开发与配置工具
    - ...
  - cmake/
    - ...
  - docs/ — 中英文文档与 Nimbus 站点
    - ...

</FileTree>

上图列出主要目录。Consul、etcd、Kafka 等现有官方插件也按同一规则组织。Lua、Actor 的接入仍在规划中，上表列出的是它们的建议实现位置。

MySQL、Redis、Utility、Net、Bus、HTTP 和 Kernel 采用同目录布局，下面以数据库插件为例：

<FileTree>

- src/plugins/mysql/
  - CMakeLists.txt
  - mysql_module.hpp — 显式公开接口；映射为 ark/mysql/mysql_module.hpp
  - mysql_module.cpp — 隐藏实现类与单一私有工厂
  - mysql_plugin.hpp — 私有插件类型和工厂声明，不进入公共头映射
  - mysql_plugin.cpp — 注册/注销与显式动态库入口
  - detail/ — 私有原生服务、协程桥和事务状态
    - ...
- tests/database/ — 协议、DSO 与显式真实服务检查
  - ...

</FileTree>

插件使用说明放在 `docs/`，示例应用放在 `examples/`。公共接口按能力定义，数据库客户端类型尽量保留在实现内。

## 核心与插件的边界

| 部分 | 职责 | 构建与发布 |
| --- | --- | --- |
| 核心运行时 | 插件与模块注册、依赖顺序及生命周期 | 与官方插件统一验证接口兼容性 |
| 官方插件 | 网络、存储、脚本、服务发现等可选服务 | 独立 CMake target，按需启用和打包 |
| 业务插件 | 产品规则与业务消息处理 | 由应用仓库独立构建和发布 |
| arknet | IO 执行、网络端点、传输协议及数据库客户端 | 独立版本，由 ark 固定依赖版本 |

启用某个插件时才解析其专属依赖；框架公共头文件避免包含 MySQL、Redis、Lua 等客户端头文件。编译依赖和运行时服务依赖需要明确区分，例如启用 Redis 插件的测试还需要可用的 Redis 服务。

## 数据库客户端的职责

MySQL 和 Redis 客户端在 arknet 中自主实现，不采用 Boost.MySQL 或 Boost.Redis。arknet 负责数据库协议编解码、认证和异步通信；ark 的插件负责框架配置、模块服务和业务回调接入。

arknet 的单节点客户端范围为：MySQL 提供查询、参数化预处理和事务；Redis 提供通用命令、RESP2／RESP3 和流水线请求。两者包含认证、TLS、连接池、超时、取消和队列上限。Redis Cluster、Sentinel 和 Pub/Sub 后续再加。ark 已通过公开协程接口接入原生客户端，并保留协议端点、隐藏符号 DSO 及显式真实服务验收入口；已覆盖与未覆盖场景见[数据库测试指南](../../tests/database/README_CN.md)。这不证明任意部署、旧动态库兼容或生产容量。

## 接下来讨论

1. 保持已统一的 snake_case 公共插件接口和目录结构；公开头文件与 CMake target 见[项目结构](../reference/directory-structure_CN.md)。
2. MySQL、Redis 的部署验证应与公开协程接口和 arknet 原生拥有型结果保持一致。arknet 提供调用方驱动的执行器，ark 在模块线程中派发；部署新的服务配置前，运行显式真实服务验收模式。
3. 分别确认 Lua、Actor 等官方插件的接口和依赖，按插件补齐编译、功能和异常路径验证。

热更新相关的状态持有、调用完成判断和兼容边界，待当前改造完成后继续讨论。

## Utility 创建与兼容性

Utility 的四个模块公开接口与调度辅助头位于 `src/plugins/utility/`，由 `ark::utility` 显式映射。各模块实现类只在对应 cpp 中定义，通过私有工厂返回公开接口。插件采用 `plugin_base` 与显式的 `DllEntryPlugin`/`DllExitPlugin` 入口，只有 timer 模块参与管理器更新。接口与迁移方式见 [Utility 参考](../reference/utility_CN.md)。

移除 `ark/utility/*_module_impl.hpp` 与 `ark/utility/utility_plugin.hpp` 是源码兼容性变更。使用公开模块接口和类型化查找，不再直接构造实现类。Utility 与使用方需要一起重新编译；本次不保证旧实现类二进制兼容。

Kernel 的原有 38 个头与 cpp 共置于 `src/plugins/kernel/`，通过 `ark::kernel` 保留全部 `<ark/kernel/...>` 路径，包括 `data_impl.hpp`、`data_list_impl.hpp` 和 `kernel_plugin.hpp`。Kernel 使用类型化模块／插件基类、私有工厂与显式入口，同时保留公开实现类／插件类。受影响的消费者需要一起重建，类布局兼容边界见 [Kernel 构造](../reference/kernel_CN.md#kernel-类型化构造)。消费检查及 Protobuf 条件见 [Kernel 参考](../reference/kernel_CN.md)。
