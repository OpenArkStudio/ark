# 快速开始

运行 Timers，确认应用能够加载插件并进入模块生命周期。它只使用 `ark_utility_plugin` 和 `timers_plugin`，不需要 Consul、数据库服务或 Excel 配置生成步骤。

<Steps>

<Step>

## 编译应用和插件

先完成[安装与构建](installation_CN.md)。以下命令在仓库根目录执行，使用该文档中的 Release 输出目录 `app-build/bin/`。程序和插件文件应来自同一次构建。

</Step>

<Step>

## 准备运行目录

在独立运行目录中放置程序、应用清单和日志配置，与源码配置分开。

<Tabs syncKey="timers-platform">

<TabItem label="macOS/Linux">

```sh
mkdir -p run/timers/bin run/timers/conf
cp -R app-build/bin/. run/timers/bin/
cp config/timers.app.toml config/log.conf run/timers/conf/
```

</TabItem>

<TabItem label="Windows PowerShell">

```powershell
New-Item -ItemType Directory -Path run/timers/bin, run/timers/conf -Force | Out-Null
Copy-Item app-build/bin/* run/timers/bin/ -Recurse -Force
Copy-Item config/timers.app.toml, config/log.conf run/timers/conf/
```

</TabItem>

</Tabs>

运行所需的文件如下：

```text
run/timers/
├── bin/       同一次构建的 app、ark_utility_plugin 和 timers_plugin
└── conf/
    ├── timers.app.toml
    └── log.conf
```

复制命令会带入同次构建的其他产物；`timers.app.toml` 只选择上述两个库，无需单独的 `timers.conf` 或 `resource/`。

</Step>

<Step>

## 启动 Timers

进入 `run/timers/` 后启动。应用配置中的 `bin/` 和 `conf/` 都相对于这个工作目录。

<Tabs syncKey="timers-platform">

<TabItem label="macOS/Linux">

```sh
cd run/timers
./bin/app --busid=1.0.1.1 --name=timers \
  --app_conf=conf/timers.app.toml --log_conf=conf/log.conf
```

</TabItem>

<TabItem label="Windows PowerShell">

```powershell
cd run/timers
.\bin\app.exe --busid=1.0.1.1 --name=timers `
  --app_conf=conf/timers.app.toml --log_conf=conf/log.conf
```

</TabItem>

</Tabs>

Debug 程序名为 `app_d` 或 `app_d.exe`，插件文件在平台后缀前带有 `_d`。将整套 Debug 产物放入独立运行目录，XML 中的插件名称保持不变。

</Step>

<Step>

## 确认运行结果

控制台应出现包含 `timers_module` 和 `PostInit` 的日志。示例注册的定时器每十秒输出一次 `timer log`，共五次。附带的日志配置还会在运行目录的 `binlog/` 下写入文件。

macOS/Linux 使用 Ctrl+C 停止，Windows 在应用控制台输入 `exit`。Timers 应依次输出 `PreShut`、`Shut`。

<Aside type="caution" title="处理启动失败">

插件加载失败，或模块启动回调返回 `false`、抛出异常时，启动器记录失败阶段并以退出码 `1` 结束，不进入后续启动阶段或更新循环。按[应用启动器](../guides/applications_CN.md#排查启动问题)中的步骤检查配置和动态库错误。

</Aside>

</Step>

</Steps>

## 接入自己的应用

- [创建模块](../guides/modules_CN.md)和[创建插件](../guides/plugins_CN.md)说明如何注册功能。
- [应用启动器](../guides/applications_CN.md)说明 Bus 标识、插件选择和配置路径。使用 Kernel 数据的应用还需要生成的 `resource/` 文件。
- [配置工具](../guides/tools_CN.md)提供独立的 Excel 资源导出流程，其 Python 依赖不属于本示例或 C++ 网络测试的运行要求。
- [网络回归测试](../networking_CN.md)可独立验证网络行为。选择 Consul 或数据库插件的应用需要另外准备对应服务和配置。

## 阅读使用新基类的示例装配

`examples/timers`、`examples/module_lifecycle` 和 `examples/entity_operations` 使用
[`module_base<interface_type>`](../guides/modules_CN.md#用基类隐藏具体实现)
及 [`plugin_base<derived_type>`](../guides/plugins_CN.md#用类型化注册和显式入口)。
示例类仍放在插件同目录的头文件中，不额外引入服务接口，保留对应的能力模块 RTTI 注册键。Timers 改为继承 `module_base<module, timer_module, guid_module>`，声明实际必需的 Utility provider；另两个示例保留 `module_base<module>`。依赖验证发生在启动钩子前，每个启动阶段先执行 provider。见[启动依赖与重建要求](../guides/modules_CN.md#声明启动依赖)。

以 `timers_plugin.hpp` 为例，它声明返回 `std::unique_ptr<timers_module>` 的工厂，
由 `timers_module.cpp` 定义。插件注册工厂返回的对象，并显式导出
`DllEntryPlugin` / `DllExitPlugin`，保留原有 logger 初始化。注册成功后仍需在
`uninstall()` 中显式注销并删除模块。

| 示例 | update 注册 | 运行依赖 |
| --- | --- | --- |
| Timers | `module_update::enabled`，保留其 `update()` 覆写行为 | Utility 插件，无需外部业务元数据 |
| `module_lifecycle` | `module_update::disabled`，保留其继承的默认 `update()` 行为 | Kernel、Utility 插件及 `resource/meta/` 下的业务元数据 |
| `entity_operations` | `module_update::disabled`，保留其继承的默认 `update()` 行为 | Kernel、Utility 插件及附带的小型示例元数据 |

timers 与 module_lifecycle 的业务方法体保留；实体示例改为轻量数据演示，旧计时负载移入显式基准。能力命名会改变模块 RTTI 注册键与配置中的动态库名称。`module_lifecycle` 的元数据属于外部
业务数据，本次装配调整不生成或转换这些数据。启用应用、示例、配置和 core 测试，
并启用所需 Kernel/Utility 插件后，执行
`cmake --build app-build --target check-typed-examples -j2`，可加载三个示例的真实
动态库，在模块管理器所属线程更新五次，并检查启动、停服顺序。该测试为 `module_lifecycle` 提供小型
测试元数据，不代表已验证应用导出的业务数据集。

## 使用安装产物

完成[安装](installation_CN.md#安装并消费-cmake-包)后，将复制命令中的 `app-build/bin/` 替换为所选前缀下的 `bin/`，从 `<prefix>/share/ark/config/` 复制配置样例。安装目录本身不是 Timers 的工作目录：仍须按上文准备 `run/timers/conf/`，并在 `run/timers/` 下启动。迁移后的包仍需兼容的外部运行时库。安装消费者检查验证 CMake 与链接消费，不代表应用部署已经验收。
