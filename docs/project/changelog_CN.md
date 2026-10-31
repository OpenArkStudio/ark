# Changelogs

## 未发布

### 功能

- 应用与日志设置共用一个清单；省略日志设置时提供控制台 INFO 默认日志，可在同一文件启用文件输出、格式和具名日志。

- Net 在初始化前检查必需的 Bus 与 Timer 模块，并按依赖关系安排停止接收阶段的回调顺序。

### 升级说明

- 移除启动参数 `--log_conf` / `-l`，将旧日志配置迁入应用清单的 `[logging]`；显式传入旧参数会以退出码 `1` 结束。公开 `application_manifest` 和日志类型布局改变，应用、插件及使用方必须一起重新编译。详见[迁移说明](../service-manifests_CN.md#内嵌日志配置)。

- 源码运行资产移出 `build/`：配置样例改为 `config/`，业务资源改为 `resources/`，离线工具改为 `tools/`，四个启动/停止脚本改为 `deployment/scripts/`。更新外部脚本与复制来源；运行包仍使用 `bin/`、`conf/` 和 `resource/`。`build/` 仅作为用户选择的 CMake 构建目录，已有构建产物不需要移动。

- 容器构建文件与 Kubernetes 示例已集中到 `deployment/`。从仓库根目录执行
  `docker build -f deployment/Dockerfile .`，并更新外部命令中的旧 `docker/`、
  `kubernetes/` 路径。
- 运行脚本现在按显式构建模式、名称和总线 ID 启动一个前台应用。将 `rund.*` 与示例包装
  脚本改为 `run.* Debug ...`；停止脚本要求显式传入一个 PID。旧五服务启动顺序与
  Visual Studio 清理命令的替代方式见[运行脚本迁移](../guides/applications_CN.md#迁移旧命令)。

### version 0.9

- Fix `HeartBeatManager` remove mistake(Thanks @sks)
- Add `DateTime`
- Fix sonar bad smells
- Replace `AFBitValue` with `std::bitset`
- Add `random` functions
- Replace `easylogging++` with `spdlog`
- Add BusPlugin(use `busid` as server id and communicate, for now not totally finished)
- Update `Proto` library to `ProtoPlugin`
- Update `Net` library to `NetPlugin`
- Update `brynet` to the newest version
- Refactoring the net plugin
- Add DataTable callback when `DataTable.Add/Set/Del(data)`
- Modify table row index
- Modify module register order and fix config's bug
- Optimise - Don't load DataNode in AFConfigModule when the node value is the default
- Add AFCMsgModule(not finished yet)

### version 0.8

- Replace `evpp` with `brynet` for reducing dependence
- Add `AFCronScheduleModule`, using `cron` syntax
- Add `WebSocket` support
- Replace `easylogging++` with `spdlog`
- Add `SonarCloud` check
- Add `Codecov` test
- Fix some warnings
- Fix some code smells of SonarCloud
- Fix GUID bug
- Fix some known bugs

### version 0.7

- Restructure log module
- Update to VS2017 as default IDE in windows
- Add function  `strlcpy`
- Use a new memory pool
- Fix `AFTimer` bugs
- Reduce `.git` disk usage
- Fix Net bugs
- Fix parameters warnings
- Fix tools, config id can be a number
- Rename Example files

### version 0.6

- Change Mem allocation
- Add Timer Module(Using time wheel)
- Modify Plugin DLL entry and exit functions by using macro `ARK_DLL_PLUGIN_ENTRY(Plugin Class)`
- Add `ARK_STATIC_ASSERT`
- Add launch parameters for launching multi-processes
- Use `std::is_base_of` to instead self derived class check

### version 0.5

- Fix some memory bugs
- Add memory pool and allocation
- Add wiki(Unfinished)
- Restructure file directories
- Add a new file processing tool

### version 0.4

- Unified concept, Property -> DataNode Record -> DataTable
- Unified configurations
- Update Google.Protobuffer 3.x
- Fix some bugs

### version 0.3

- Fix msg head bugs
- Rename some files

### version 0.2

- Modify base data structure
- Modify the performance of the network
- Modify directories structure
- Add CMake support(Linux platform is testing)
- Modify the compiling way of **Dep**
- Modify GUID to 128 bits

### version 0.1

- First release
