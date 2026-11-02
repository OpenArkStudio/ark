# 应用启动器

`app` 设置进程标识、读取 TOML 或 XML 应用清单并加载插件库。首次运行见 [Timers 快速开始](../get-started/quick-start_CN.md)；本文说明配置路径、库名和启动检查。

## 命令行参数

| 参数 | 必填 | 含义 |
| --- | --- | --- |
| `--busid` / `-b` | 是 | 进程的逻辑 Bus 地址，例如 `1.0.1.1`；不是 socket 绑定地址 |
| `--name` / `-n` | 是 | 应用名称，也用于日志文件名 |
| `--app_conf` / `-a` | 是 | 扩展名为小写 `.toml` 的 TOML 应用配置，或旧版 XML `*.app.conf` 文件 |
| `--help` / `-h` | 否 | 输出启动器的参数说明 |

```sh
./bin/app --busid=1.0.1.1 --name=timers \
  --app_conf=conf/timers.app.toml
```

`busid`、`name` 和 `app_conf` 都需要显式传入。地址各字段的含义见 [Bus 参考](../reference/bus_CN.md)。其他平台相关参数以程序的 `--help` 输出为准。

`--log_conf` / `-l` 已移除。显式传入任一参数会输出迁移提示并以退出码 `1` 结束。将日志设置迁入应用清单；启动器不再读取单独的 `log.conf`。详见[内嵌日志配置](../service-manifests_CN.md#内嵌日志配置)。公开类型 `application_manifest` 的布局已改变，应用、插件和使用方必须一起重新编译。

## 确定配置路径

相对路径以进程工作目录为基准，不随程序或清单位置改变。

![从工作目录解析配置路径](../assets/launcher-paths_CN.svg)

| 输入 | 选择方式 | 相对于运行目录的示例 |
| --- | --- | --- |
| 应用配置 | `--app_conf` | `conf/timers.app.toml` |
| 日志配置 | 应用清单中可选的 `[logging]` | 省略时使用控制台 INFO 日志 |
| 插件动态库 | TOML `plugin_library_dir` 或 XML `plugin_lib_dir` 加动态库文件名 | macOS Release 使用 `bin/timers_plugin.dylib` |
| 插件配置 | TOML `plugin_config_dir` + `config`，或 XML `plugin_conf_dir` + `conf` | 配置文件名为 `my-plugin.conf` 时，对应 `conf/my-plugin.conf` |
| Kernel 元数据和配置 | Kernel 使用的 `resource/` 目录 | `resource/meta/entity_class.config`、`resource/server/item.config` |

启动器把插件配置路径交给插件管理器，具体模块自行读取配置。Kernel 资源路径与应用配置路径相互独立，只有使用相关 Kernel 能力的应用才需要这些资源。详见[数据驱动配置](../concepts/data-driven_CN.md)和[配置工具](tools_CN.md)。

## 选择插件

内置应用配置采用 TOML。仓库中的 [timers.app.toml](https://github.com/OpenArkStudio/ark/blob/main/config/timers.app.toml) 为：

```toml
[application]
plugin_library_dir = "bin/"
plugin_config_dir = "conf/"

[[plugins]]
name = "ark_utility_plugin"

[[plugins]]
name = "timers_plugin"
```

通过 `--app_conf=conf/timers.app.toml` 选择该文件。`application` 表必须存在，目录字段为可省略的字符串。每个插件的 `name` 必须非空且唯一；可选的 `config` 字符串指定插件配置文件。字段校验和格式边界见[应用配置清单](../service-manifests_CN.md)。Bus、HTTP 和 Consul 插件配置使用 TOML，Bus 字段见[运行配置](../reference/bus_CN.md#配置进程与连接关系)。日志与应用共用清单；Kernel 元数据和业务导出数据保留 XML 格式。

### 插件库命名

插件构建目标和库文件名统一使用小写 `snake_case`，例如 `ark_utility_plugin`、
`ark_mysql_plugin` 和 `timers_plugin`。配置中的插件名必须与库文件名一致；
加载器自动添加平台扩展名和 Debug 的 `_d` 后缀。
已有配置和构建命令中的 `AFUtilityPlugin`、`AFMysqlPlugin`、`Sample1Plugin` 需要更新，
构建不再生成这些旧文件名。

### 兼容已有 XML 配置

启动器仍接受用户维护的旧版 XML 清单，例如：

```xml
<xml>
    <plugins plugin_lib_dir="bin/" plugin_conf_dir="conf/">
        <plugin name="ark_utility_plugin" conf="" />
        <plugin name="timers_plugin" conf="" />
    </plugins>
</xml>
```

- 保留外层 `xml` 元素，启动器在其下查找 `plugins` 子元素。
- `bin/`、`conf/` 等目录前缀保留末尾分隔符，当前加载器直接拼接字符串。
- `name` 使用动态库名称主体，不加平台后缀，也不加 `lib` 前缀。加载器追加 `.dll`、`.so` 或 `.dylib`，Debug 构建还会在后缀前追加 `_d`。
- `conf` 为空表示没有提供插件配置文件名。Timers 不需要单独的 `timers.conf`。

程序和所选插件动态库应来自同一次构建，Debug 与 Release 文件分别放置。插件条目顺序不代表依赖初始化顺序；模块通过[模块 API](modules_CN.md#声明启动依赖)声明必需 provider 注册键。管理器在安装完成后验证依赖，并按依赖顺序执行四个启动阶段，按反向缓存顺序逐个清理并销毁消费者，再处理提供方，全部动态库最后关闭。移除模块停止接收虚方法，管理器布局也改变；必须同时重建启动器和全部插件，不得混用旧二进制。

## 确认启动并停止进程

Timers 的日志出现 `timers_module` 和 `PostInit`，表示示例已经进入初始化后回调，该回调负责注册定时器。

进程停止前，由应用或运维完成公告、关闭业务入口、踢人和保存。框架只负责资源安全退出，不在信号触发后编排这些业务动作。

macOS/Linux 可使用 Ctrl+C 或 `SIGTERM`，由应用主循环处理停止请求；Windows 在应用控制台输入 `exit`。启动器通过 `stop()` 进入 `stopping`，对任务组调用 `close()` 请求取消，继续模块更新与共享执行器轮询，等待协程帧、活跃回调及原生工作安全结束。

全部模块 `ready_to_shut()` 返回就绪后，管理器先按反向依赖执行全部 `pre_shut()`，在所有对象存活时关闭原生回调源；再反向逐个执行 `shut()` 并销毁拥有的对象，保证消费者先于提供方释放。所有模块释放后注销插件，全部 DSO 最后关闭。Timers 的日志仅标记其钩子执行，不证明所有插件组合都安全。未声明依赖与任意外部回调仍由资源所有者负责。

就绪异常、清理钩子 false／异常或动态库关闭失败进入 `failed`，输出一次 `shutdown:` 阶段／模块诊断并保留剩余资源，不重试或回滚。`update()` 在停机期间失败也使启动器保持资源保留。`dlclose`／`FreeLibrary` 成功不证明操作系统已移除库映像，也不提供任意热更新保证。

`--shutdown_timeout_ms=N` 接受非负 int64 毫秒数；默认 `0` 无限等待。正数通过 `stop_until()` 提供单调时钟截止时间，仅作为资源清理保护，不是业务排空预算。清理前超时进入 `timed_out`，可继续 owner 更新但不恢复清理；清理开始后超时进入 `cleanup_timed_out`，停止后续更新与清理。重复 stop 或迟到的完成不能清除超时。

时限不能中断阻塞的 update、就绪检查、清理或原生关闭，也不能撤销已执行动作。超时不允许销毁待完成帧、强制终止线程或按成功退出。见[模块任务与资源清理](tasks_CN.md#模块共用业务执行器)。

<Aside type="caution" title="进程存在不等于应用就绪">

接入流量前，检查目标模块的初始化日志与应用就绪条件；PID 存在或退出码为零均不足以判断就绪。

| 失败路径 | 诊断与退出行为 |
| --- | --- |
| 应用清单读取或校验失败 | 插件启动前退出，状态码 `1` |
| `plugin_manager::start()` 返回 `false`，或模块／插件启动异常向外传播 | 同步输出并刷新失败阶段、模块／插件名及可用异常信息；以 `1` 退出，不进入更新循环或启动 Windows 命令线程 |
| 缺少库或 `DllEntryPlugin` | 报告 `load_plugin: <plugin>` 并以 `1` 退出；已加载但缺少入口的库保留至进程退出，不自动卸载 |
| 参数解析失败（包括帮助处理） | 仍返回 `0` |

`start()` 失败或启动异常传播时，跳过 C++ 对象与静态析构，避免部分启动的 IO 池阻止退出；其他缓冲／异步日志可能丢失。不回滚部分启动工作、不执行正常关闭钩子、不在同进程重试；部署工具应启动新进程。

</Aside>

启动器用 1024 字节栈缓冲和原生 stderr 写入保留启动失败诊断，超长文本截断，持续 C++ 分配失败也继续退出。获取前预登记的最多 16 个独立 owning capsule 可按登记逆序清理；遇到 unsafe／failed／异常立即停止，不再清理更早所有者。随后仍退出 `1`，不调用正常停服或卸载。Utility Timer 已登记受限清理，存在业务回调时保留资源；共享 IO 不自动清理，见[启动失败清理](../project/startup-failure-cleanup_CN.md)。

资源生命周期使用 `check-app-business-runtime`、`check-app-shutdown-deadlines` 和 `check-native-unload` 等永久套件；验收以实际运行的修订和结果为准。旧停止接收／自然排空场景不作为当前取消式资源清理的证据。见[生命周期状态](../project/lifecycle-closeout_CN.md)。

## 启动一个应用

Shell 和 Windows 脚本使用相同参数：

```text
run.sh  <Release|Debug> <name> <busid> [runtime-directory]
run.bat <Release|Debug> <name> <busid> [runtime-directory]
```

运行目录默认为脚本所在目录。从源码仓库调用 `deployment/scripts/` 下的脚本时，显式传入已准备的运行目录；也可将脚本复制到运行目录根部后使用默认值。显式传入的相对路径以调用者的工作目录为基准。
Release 使用 `bin/app`（Windows 为 `app.exe`），Debug 使用 `bin/app_d`
（Windows 为 `app_d.exe`）。脚本读取 `conf/<name>.app.toml`，
在前台运行应用；`name` 同时作为 `--name` 的值。使用旧版 XML、自定义清单路径、不同的进程名称或
其他启动参数时，直接调用程序。

按[Timers 快速开始](../get-started/quick-start_CN.md)准备运行目录后，在仓库根目录执行：

```sh
sh deployment/scripts/run.sh Release timers 1.0.1.1 run/timers
```

Windows 使用 `deployment\scripts\run.bat Release timers 1.0.1.1 run\timers`。
使用应用前，检查初始化日志。脚本的路径检查不验证插件、业务配置或服务就绪状态。

## 停止一个应用

macOS/Linux 优先使用 Ctrl+C，Windows 优先在应用控制台输入 `exit`。
确认目标进程的 PID 后，使用 `kill.sh <pid>` 请求 `SIGTERM`；`kill.bat <pid>` 会强制终止
该 PID。两个脚本都只接受一个显式 PID，并拒绝其他可执行文件名。执行前重新确认 PID 的
可执行文件与命令行；进程退出后，PID 可能被复用。脚本不按名称通配批量停止进程。

正常停服时，启动器向标准输出写入并刷新 `ARK Server stop ...`，然后以退出码 `0`
结束。这条进程状态消息使用同步输出。

## 迁移旧命令

- 将 `rund.sh` / `rund.bat` 改为 `run.sh Debug ...` / `run.bat Debug ...`。
- 将 `sample1.sh`、`mysql_sample.sh` 和 `sample5http_test.sh` 改为
  `run.sh Debug <name> <busid>`；名称分别为 `timers`、`mysql_sample`、`http_server`。该名称映射
  不会启用未构建的示例目标。MySQL/Redis 示例在默认构建中仍未启用，其清单不提供数据库连接选项；
  使用时需要显式配置公开数据库模块。
- 旧版无参数五服务启动顺序引用了不存在的 `plugin_conf/`，Release 与 Debug 的总线 ID
  也不一致。现在需要显式传入每个应用的名称与总线 ID，并按目标环境安排启动顺序。
  新脚本不再将应用转为守护进程。
- 使用 `cmake --build <cmake-build-directory> --target clean` 清理选定构建目录，
  替代 `vc_clear.bat`。
- 容器构建文件和集群示例已移至 [deployment/](../../deployment/README_CN.md)。

`docs/site/legacy-source/` 按原始字节保留历史文章，其中的 `bin/rund.*` 命令不再适用，
请使用上面的当前命令。

## 排查启动问题

| 现象 | 检查项 |
| --- | --- |
| 输出帮助或 `Application parameter is invalid` | 补齐三个必填参数，用 `--help` 核对拼写 |
| 应用配置加载失败 | 检查工作目录、显式路径和文件格式；小写 `.toml` 后缀选择 TOML，旧版 `*.app.conf` 使用 XML |
| 日志校验或初始化失败 | 检查清单诊断和 `logging.output_path` 的写入权限 |
| `Load shared library[...] failed` | 核对 TOML `plugin_library_dir` 或 XML `plugin_lib_dir`、平台后缀、Debug 后缀，以及动态库自身的共享库依赖 |
| 插件已加载，但目标模块不存在 | 核对模块注册、启用的构建目标和依赖插件，详见[创建插件](plugins_CN.md) |
| 内置服务角色在 `init:` 失败 | 加载 Bus 和 Net；Game 注册回调前还需要 Kernel。已检查的范围见[角色启动错误](../concepts/server-architecture_CN.md#角色启动错误) |
| 内置服务角色在 `post_init:` 失败 | Game 需检查 Player 元数据和回调注册诊断；同时检查监听端点配置和端口占用。注册失败、创建监听器失败或服务缺失会传递到启动器，并产生退出码 `1` |
| Kernel 元数据或配置缺失 | 在进程工作目录准备生成的 `resource/meta/` 和 `resource/server/` 文件 |

启动器实现见 [main.cpp](https://github.com/OpenArkStudio/ark/blob/main/src/application/main.cpp)。采用现有多进程脚本前先检查配置，部分脚本仍沿用旧部署路径和服务设置。
