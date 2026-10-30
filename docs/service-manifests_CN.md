# 应用插件清单

应用插件清单指定要加载的插件库及配置文件，通过 `-a` / `--app_conf` 传入 `.toml` 清单或原有 `.app.conf` XML 清单。TOML 解析失败时直接报错，不会回退到 XML。

## TOML 格式

仓库中的 [timers.app.toml](../config/timers.app.toml) 采用 TOML 1.0 语法；迁移前的 XML 清单保留在解析器回归测试中：

```toml
[application]
plugin_library_dir = "bin/"
plugin_config_dir = "conf/"

[[plugins]]
name = "ark_utility_plugin"
config = ""

[[plugins]]
name = "timers_plugin"
```

| 字段 | 类型及默认值 | 含义 |
| --- | --- | --- |
| `application` | 表，必填 | 应用清单设置。 |
| `application.plugin_library_dir` | 字符串，`""` | 插件库名称前的路径前缀。 |
| `application.plugin_config_dir` | 字符串，`""` | 插件配置文件名的路径前缀。 |
| `plugins` | 表数组，空 | 省略或在顶层设置 `plugins = []` 均表示不加载插件。 |
| `plugins[].name` | 非空字符串，必填 | 唯一的动态库名称，例如 `timers_plugin`，不含扩展名；平台后缀及 Debug 的 `_d` 后缀仍由加载器补充。 |
| `plugins[].config` | 字符串，`""` | 配置文件名；省略或设置 `""` 均保留原有的空配置语义。 |

类型错误、未知字段、重复或空插件名、无效 TOML / UTF-8 和嵌入的 NUL 字符都会在插件启动前被拒绝。错误信息包含清单路径及可用的行列位置，不输出配置值。

加载器原样保留目录字符串，相对路径以进程工作目录为基准。库名和配置文件名通过字符串拼接追加到前缀后，因此目录前缀通常需要以 `/` 或平台适用的分隔符结尾，加载器不会自动补充分隔符。`config` 为空时，不要求存在对应的配置文件。

`[[plugins]]` 的书写顺序**不定义模块初始化顺序**，插件管理器仍接收无序映射。本次格式支持不引入依赖排序或运行时重新加载机制。

## 运行 timers

按[应用构建说明](networking_CN.md)构建 `app`、`ark_utility_plugin` 和 `timers_plugin`。假设构建目录为 `out`，从仓库根目录执行：

```sh
mkdir -p run/timers/bin run/timers/conf
cp -R out/bin/. run/timers/bin/
cp config/timers.app.toml run/timers/conf/
cd run/timers
./bin/app --busid=1.0.0.1 --name=timers -a conf/timers.app.toml
```

Debug 构建使用 `app_d`。Bus、HTTP 和 Consul 的运行参数使用 TOML，应用清单只指定这些文件的路径。Bus 字段见[运行配置](reference/bus_CN.md#配置进程与连接关系)，HTTP／Consul 示例见下文。日志使用同一应用清单；Kernel 和业务导出数据保留 XML 格式。

## 内嵌日志配置

日志设置可省略。未提供 `[logging]` 时，启动器使用同步控制台 INFO 日志，包含时间和源码位置，不创建日志文件，也不读取 `log.conf`。需要覆盖默认值时，在同一应用文件中添加：

```toml
[logging]
console = true
file = true
level = "info"
pattern = "%^[%Y%m%d %H:%M:%S.%e][%-5l]%v%$"
output_path = "./binlog"
queue_count = 0

[logging.loggers.oss]
output_path = "./binlog/oss"
```

| 字段 | 默认值 | 约束 |
| --- | --- | --- |
| `console` / `file` | `true` / `false` | 布尔输出开关，至少启用一项。文件输出必须显式启用，按日期目录和小时文件名轮转。 |
| `level` | `"info"` | `trace`、`debug`、`info`、`warn`、`error`、`critical` 或 `off`。 |
| `pattern` | 上例格式 | 不含 NUL 的非空 spdlog 格式字符串。 |
| `output_path` | `"./binlog"` | 不含 NUL 的非空路径；相对路径以进程工作目录为基准。应用名和 Bus ID 组成文件名。启用文件输出时自动创建父目录。 |
| `queue_count` | `0` | 0..65535 的整数；0 表示同步输出，正数启用独立异步队列。 |
| `loggers` | 空 | 具名表继承基础设置，再覆盖提供的字段。`oss` 选择 OSS 日志；未配置时 OSS 共用默认日志。其他名称可通过 `create_logger()` 使用。 |

Debug 和 Release 使用相同设置。XML 应用清单可在 `<plugins>` 旁添加 `<logging console="true" file="false" level="info" queue_count="0"><logger name="oss" level="warn"/></logging>`，属性名和默认值相同。XML 布尔值也接受 `1` 和 `0`。插件专用的服务和业务配置继续单独组织。

**不兼容迁移：**从启动命令移除 `--log_conf` / `-l`；显式提供任一参数会以退出码 `1` 结束。将旧 XML 中选用的 `debug`/`release` 设置迁入 `[logging]`：`path` 改为 `output_path`，数值日志级别 0..6 改为上述名称，`console` 改用布尔值。需要保留文件输出时设置 `file = true`；保留原有异步队列时设置 `queue_count = 1024`。新 API 的文件输出由运行时设置控制，不受 `ARK_OPEN_FILE_LOG` 影响；该构建选项仍控制旧版库接口的默认值。`application_manifest` 的公开布局已改变，应用、插件和使用方必须一起重新编译。直接调用库的 XML 重载 `logger::init_logger(path, filename)` 继续保留，启动器不使用该重载。

日志结构错误会包含应用路径和字段，并在插件加载前失败。文件初始化失败也以退出码 `1` 结束。异步输出仍受既有紧急退出行为限制；需要日志在调用内完成时使用 `queue_count = 0`。既有启动失败诊断继续通过紧急 stderr 路径输出。

## HTTP 与 Consul 插件配置

`ark_http_plugin` 读取 `http.plugin.toml`：

```toml
[http]
host = "0.0.0.0"
port = 9090
threads = 4
```

`host` 为非空地址或域名。`port` 为 0–65535 的整数；服务启动时，零表示申请可用端口。`threads` 为 1–255 的整数，与现有服务接口一致。

HTTP 插件在初始化时解析这些字段；通过[公开 HTTP 接口](reference/http_CN.md)显式启动监听。路由处理器在 arknet IO 工作线程执行，所属线程的 `pre_shut()` 停止监听。

`ark_consul_plugin` 读取 `consul.plugin.toml`：

```toml
[consul]
host = "127.0.0.1"
port = 8500
```

Consul 目标端口必须为 1–65535。未知表或字段、缺失字段、类型错误及数值越界都会使插件初始化失败。错误信息包含文件和字段；语法错误另含行列位置，不输出配置值。解析配置不会进行域名解析或建立连接。

迁移现有部署时，将 HTTP／Consul XML 文件替换为上述 TOML 配置，并修改应用清单中的配置文件名。原 `ip` 改为 `host`，HTTP 的 `thread` 或 `thread_num` 改为 `threads`。仅修改扩展名无效，这两个插件要求内容为 TOML。仓库的应用清单与部署 ConfigMap 已改用新文件名。本次修改不会让 Consul 成为必选依赖，也不调整暂缓讨论的服务发现方案。

## XML 兼容性

原有包装节点和属性名继续有效：

```xml
<xml>
    <plugins plugin_lib_dir="bin/" plugin_conf_dir="conf/">
        <plugin name="ark_utility_plugin" conf=""/>
        <plugin name="timers_plugin"/>
    </plugins>
</xml>
```

应用加载器要求根节点下包含 `plugins` 子节点，直接以 `<plugins>` 为根节点的文件不属于应用清单。目录属性和 `conf` 省略时仍为空字符串，未知 XML 属性仍被忽略。重复或空插件名和嵌入的 NUL 字符现在会在清单读取阶段被拒绝，避免将歧义或截断数据传入插件加载阶段。清单读取失败时，应用在调用 `plugin_manager::start()` 前以非零状态退出。

## C++ 和构建集成

`src/config/` 维护应用清单声明与编译后的共享配置读取器。`ark::config` 是启用位置无关代码的静态 CMake 目标，可供动态插件链接，公开头文件仅依赖 C++ 标准库。应用和 Bus／HTTP／Consul 插件共享文件读取、TOML 语法与编码解析，以及隐藏配置值的定位诊断。应用清单的结构校验保留在 `application_manifest.cpp`，各插件继续负责字段和取值范围校验。MySQL 和 Redis 继续通过接口配置连接，不新增文件格式。公开读取函数 `load_application_manifest()` 对 `.toml` 后缀使用 TOML，对其他路径使用 XML；应用的 `-a` 参数仍保留前述清单路径限制。

```cpp
#include <ark/application_manifest.hpp>

auto manifest = ark::load_application_manifest("conf/timers.app.toml");
```

```cmake
target_link_libraries(my_loader PRIVATE ark::config)
```

CMake 将源码 `src/config/application_manifest.hpp` 映射到构建树的 `src/config/public-include/ark/application_manifest.hpp`。仅使用声明时链接 `ark::config_headers`；即使 `ARK_BUILD_CONFIG=OFF`，`ark::core` 也会传递这个轻量目标。声明映射不会引入解析器或网络依赖。手写 `-I <源码>/include` 的调用需要改用目标传递的包含目录。

构建应用或核心测试时，`ARK_BUILD_CONFIG` 默认为 `ON`，其他情况下默认为 `OFF`；应用构建要求启用此选项。网络回归使用 HTTP 实现，因此即使应用与 `ARK_BUILD_CONFIG` 关闭，也会创建 `ark::config`。目标内部使用 MIT 许可的 [toml++](https://github.com/marzer/tomlplusplus)：CMake 优先查找已安装的 3.4 或更新版本的 `tomlplusplus` 包，否则获取固定提交 `30172438cee64926dc41fdd9c11fb3ba5b2ba9de` 对应的 v3.4.0。此目标不会向 `ark::core` / `ark::ark` 的使用者传播解析器头文件和宏。XML 继续使用已有的 RapidXML 依赖。

运行 `cmake --build out --target check-application-manifest check-plugin-configuration check-bus-configuration` 可执行原生回归；`ARK_BUILD_CONFIG=ON` 时，`check-core` 也会运行这些测试。

独立配置消费工程不配置 arknet、核心或运行时目标，验证应用与插件配置的成功和失败路径，并通过动态库调用两种读取器：

```sh
cmake -S tests/consumer/config -B config-build -G Ninja
cmake --build config-build --target check-config
cmake -S tests/consumer/config -B config-headers-build -G Ninja -DARK_BUILD_CONFIG=OFF
cmake --build config-headers-build --target check-config-headers
```

仅声明构建不查找 toml++ 或 RapidXML；编译读取器时，需要与 `ark::config` 相同的解析器和 RapidXML 依赖。这两种独立配置构建都不需要网络依赖。

## 内置应用清单迁移

内置的 24 份可解析应用清单由 `config/<name>.app.conf` 改为
`config/<name>.app.toml`。这是文件路径兼容性变更：重新复制清单，更新启动参数、
部署挂载键和外部脚本中的路径。`deployment/scripts/run.sh` 与 `run.bat` 现在只查找
`conf/<name>.app.toml`，不会自动回退到 XML；需要保留旧 XML 清单时，直接向启动器
传入其原有 `--app_conf` 路径。运行时目录仍为 `conf/`。

| 原 XML 字段 | TOML 字段 | 保留的语义 |
| --- | --- | --- |
| `plugins/@plugin_lib_dir` | `application.plugin_library_dir` | 原字符串，不补目录分隔符 |
| `plugins/@plugin_conf_dir` | `application.plugin_config_dir` | 原字符串，缺省为空 |
| `plugin/@name` | `plugins[].name` | 原动态库名，不改大小写或平台后缀规则 |
| `plugin/@conf` | `plugins[].config` | 原配置文件名，缺省为空 |

每份清单保留原插件声明顺序；管理器仍使用无序映射，声明顺序不构成模块生命周期顺序保证。
`world` 的旧 `plugins/@path` 和部分清单的 `res` 节点原本不由应用清单读取器处理；
迁移不为它们增加含义，`world` 的两个目录前缀和各插件配置名继续为空。
Kernel 和业务数据 XML、未知插件配置解析器保持原格式。

ConfigMap 中相应应用键改为 `.app.toml`，包括仅用于部署示例的三份 `photon.*` 清单；
Photon 的启动参数同步使用这些键，外部业务插件名和插件配置名保持不变。
Freebird 是明确排除项：它依赖外部 `AFFreeBirdPlugin` 和仓库未提供的
`freebird.plugin.conf`，旧清单直接以 `plugins` 为根，不能通过当前应用清单读取器；
ConfigMap 中该示例另有大小写不匹配的结束标签。保留这些旧文件，不把格式迁移当作
已修复或验收过该外部应用。
