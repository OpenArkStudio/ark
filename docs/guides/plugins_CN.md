# 创建插件

插件装配一组模块，并导出启动器调用的动态库入口。官方插件放在 ark；业务插件与应用同仓库。使用[模块示例](modules_CN.md#定义接口与实现)的 `sample_module.hpp`、`sample_factory.hpp`、`sample_module.cpp`，加上本页的 `sample_plugin.cpp`、`CMakeLists.txt`，全部按功能共置。

## 声明插件

将 `sample_plugin` 具体类放在下面的 cpp，不需要公开插件头。`plugin_base<derived_type>` 提供版本 `0`、`typeid(derived_type).name()` 标识、manager 访问及 `get_plugin_conf()`／`set_plugin_conf()`。配置在同一派生类型的实例间共享，不同类型间隔离；安装前设置 manager，注册期间不得更换。

## 配对注册与注销

保存为 `sample_plugin.cpp`。工厂独占返回接口，安装时显式指定 update，注销时先消费者、后 provider：

```cpp
#include <stdexcept>

#include <ark/plugin_base.hpp>

#include "sample_factory.hpp"

namespace ark {
namespace {

class sample_plugin final : public plugin_base<sample_plugin>
{
public:
    void install() override {
        if (!register_module<counter_module>(make_counter(), module_update::disabled))
        {
            throw std::runtime_error("counter registration rejected");
        }
        if (!register_module<sample_module>(make_sample(), module_update::enabled))
        {
            throw std::runtime_error("sample registration rejected");
        }
    }

    void uninstall() override {
        if (!unregister_module<sample_module>())
        {
            throw std::runtime_error("sample unregistration rejected");
        }
        if (!unregister_module<counter_module>())
        {
            throw std::runtime_error("counter unregistration rejected");
        }
    }
};

} // namespace

ARK_EXPORT_FUNC void DllEntryPlugin(plugin_manager* manager,
    const std::string& stem, logger* log) {
    logger::init(log);
    manager->register_plugin<sample_plugin>(stem);
}

ARK_EXPORT_FUNC void DllExitPlugin(plugin_manager* manager) {
    manager->unregister_plugin<sample_plugin>();
}

} // namespace ark
```

| 操作 | 契约 |
| --- | --- |
| `register_module<interface_type>(unique_ptr, module_update)` | 消耗独占候选对象，保留接口地址，以 `typeid(interface_type).name()` 注册；省略模板参数时由工厂返回类型决定接口 |
| `module_update::enabled`／`disabled` | 必须显式选择；`disabled` 即使实现覆写 `update()` 也不更新，不检测覆写、不提供默认值 |
| `unregister_module<interface_type>()` | 与成功注册配对，成功时移除记录并删除对象一次；按模块所需顺序实现 `uninstall()`，析构插件本身不卸载模块 |

<Aside type="note" title="每个库只导出一对入口">

每个动态库仅在一个翻译单元中定义 `DllEntryPlugin`／`DllExitPlugin`，使用必要的 `ARK_EXPORT_FUNC` 平台导出宏。重复定义会造成符号冲突。入口仍传递 C++ 类型，不提供纯 C、跨编译器或跨版本 ABI 保证。此处注销顺序只约束本插件，不建立跨插件清理顺序。

</Aside>

## 构建动态库

安装 ark 的[构建依赖](../get-started/installation_CN.md)，再把下面的代码保存为业务插件目录中的 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.21)
project(sample_business LANGUAGES CXX)

set(ARK_SOURCE_DIR "" CACHE PATH "ark source checkout")
set(ARK_BUILD_APPLICATIONS OFF CACHE BOOL "Build ark applications" FORCE)
set(ARK_BUILD_PROTO OFF CACHE BOOL "Build protocol messages" FORCE)
set(ARK_BUILD_TESTS OFF CACHE BOOL "Build ark tests" FORCE)
set(ARK_BUILD_CORE_TESTS OFF CACHE BOOL "Build core tests" FORCE)
set(ARK_BUILD_EXAMPLES OFF CACHE BOOL "Build ark examples" FORCE)
add_subdirectory("${ARK_SOURCE_DIR}" ark)

add_library(sample_plugin SHARED sample_plugin.cpp sample_module.cpp)
target_link_libraries(sample_plugin PRIVATE ark::core)
set_target_properties(sample_plugin PROPERTIES
    PREFIX ""
    DEBUG_POSTFIX "_d"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
```

配置时传入 ark 源码目录。下面的命令同时适用于单配置生成器，以及 Visual Studio 等多配置生成器：

```sh
cmake -S . -B out -DARK_SOURCE_DIR="path/to/ark" -DCMAKE_BUILD_TYPE=Release
cmake --build out --config Release --target sample_plugin
```

这个构建关闭 ark 自带的应用与示例，不影响外部项目的 `sample_plugin` 目标。`ark::core` 提供公开头文件、C++20 要求和传递依赖。接口使用官方插件 API 时，再增加 `ark::net` 等接口目标。完整目标列表见[项目结构与公共插件接口](../reference/directory-structure_CN.md)。

## 在运行时选择动态库

构建要求与运行选择是不同的步骤：

| 配置 | 作用 |
| --- | --- |
| `target_link_libraries(sample_plugin PRIVATE ark::net)` | 提供网络 API 头文件和编译要求，不会加载网络插件 |
| 在 ark 应用构建中设置 `ARK_BUILD_APPLICATIONS=ON` 与 `ARK_BUILD_PLUGIN_NET=ON` | 构建选中的官方插件动态库 |
| 在应用清单中增加插件条目 | 让启动器在启动时请求加载这个动态库 |

将这个 Release 示例生成的动态库从 `out/bin/Release/` 复制到部署目录的 `bin/`，并将下面的清单保存为 `conf/sample.app.toml`。Debug 构建产物单独放在 `out/bin/Debug/`：

```toml
[application]
plugin_library_dir = "bin/"
plugin_config_dir = "conf/"

[[plugins]]
name = "sample_plugin"
config = ""
```

通过[应用启动器](applications_CN.md)选择这个清单。`sample_plugin` 是动态库文件名的主体，不是 C++ 插件类的 RTTI 名称，也不是模块注册名。加载器会补充 `.dll`、`.so` 或 `.dylib` 后缀；Debug 模式还会在扩展名前补充 `_d`。CMake 目标显式移除了 `lib` 前缀，并设置了相同的 Debug 后缀。启动器和插件应使用相同构建模式，以及兼容的公共插件接口与 ABI 设置。

路径相对于进程工作目录解析。当前加载器直接拼接 `plugin_library_dir` 与库文件名，以及 `plugin_config_dir` 与 `config`，因此目录末尾需要保留分隔符。示例中的空 `config` 表示插件没有需要读取的配置文件，但插件收到的解析路径仍是配置目录前缀，不是空字符串。提供配置文件时，可通过 `sample_plugin::get_plugin_conf()` 读取这个路径。

Windows 库路径使用 UTF-8。加载器先转换为使用本地分隔符的绝对宽字符路径，再调用 `LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)`。这样，在可执行文件和工作目录位于其他位置时，也能查找插件旁的依赖，并保留既有的 altered-search 策略和 PATH 处理；不设置进程级 DLL 目录，也不把依赖复制到各个可执行文件旁。加载失败时保留 `GetLastError()`，供管理器输出原生诊断；错误 126 也可能表示某个依赖缺失，不能仅据此认定主 DLL 不存在。非 Windows 加载方式不变。按[协议部署约定](../reference/protobuf_CN.md#构建与包含)部署匹配的 `AFProto` DLL 及其他依赖。

## 排查模块不可用

| 现象 | 检查项 |
| --- | --- |
| 无法打开动态库 | 部署工作目录、文件名前缀与扩展名、Debug 后缀及动态库依赖 |
| 找不到入口 | 显式入口是否只定义一次，导出符号是否可见 |
| 查找模块触发断言，或关闭断言时返回 `nullptr` | 运行清单是否包含提供模块的库，`install()` 是否以 `T` 注册，调用方与提供方是否使用兼容接口 |
| 头文件可以编译，但没有可用服务 | 接口目标只提供头文件，不会在运行时安装模块 |

管理器不能保证任意未完成回调下的插件卸载安全。应按[创建模块](modules_CN.md)中的约束完成服务取消和对象生命周期管理。这个示例没有工作线程任务、事件订阅或热更新能力。

## 用类型化注册和显式入口

上面的[完整示例](#配对注册与注销)是推荐写法。已有宏与插件仍受支持；迁移时保持接口键、update 选择、生命周期、排空与卸载合同：

| 旧写法 | 推荐替换 |
| --- | --- |
| `ARK_DECLARE_MODULE_FUNCTIONS` | cpp 内继承 `module_base<interface_type, providers...>`，公开头只保留接口 |
| `ARK_DECLARE_PLUGIN` | cpp 内继承 `plugin_base<derived_type>`；保留 manager／配置语义 |
| `ARK_REGISTER_MODULE`／`ARK_UNREGISTER_MODULE` | 私有工厂返回 `std::unique_ptr<interface_type>`；将旧宏按覆写检测 update 的行为改为显式开关，反向配对注销 |
| `ARK_DECLARE_PLUGIN_DLL_FUNCTION` | 显式定义入口／出口，继续使用 `ARK_EXPORT_FUNC` |

### 注册失败与资源保留

| 边界 | 行为与调用方责任 |
| --- | --- |
| 候选所有权 | 对象尚未发布且由 `unique_ptr` 独占；转交前移除旧的借用观察记录 |
| 注册被拒绝 | 空候选、重复模块标识、已占用 update 标识或已注册地址返回 false；候选析构一次，不留下借用指针 |
| 注册异常 | 元数据／分配异常及 owner 线程、回调阶段、停服检查向上传播；不撤销此前成功注册，也不运行清理钩子 |
| 注销被拒绝 | 缺少自有标识或 registry 标识不匹配返回 false；活跃回调或未完成受管任务触发原有保护，保留对象与记录 |
| 后续安装失败 | 已注册资源保留至启动失败退出，不在同进程回滚或重试；[启动器](applications_CN.md#确认启动并停止进程)报告失败并退出 `1` |
| 插件分配／元数据 | `register_plugin<T>()` 保留 `ARK_NEW`／`ARK_DELETE`（含可空 nothrow 分配）；空分配不安装，记录 `allocate_plugin: <library stem>`，`start()` 返回 false，不加载后续库或进入模块阶段；安装前元数据异常只析构候选一次，安装后遵循失败退出策略 |
| 动态库包装器 | 发布至管理器前局部持有；原生加载失败即释放，即使日志抛异常；已加载句柄仍显式卸载，调用方无需迁移 |

以下插件使用类型化构造。表中公开布局发生变化的插件与消费者必须一起重建；Net 移除公开实现头，也须重建受影响消费者。各项约束始终可见：

| 插件 | 公开接口与构造 | 保留行为与迁移边界 |
| --- | --- | --- |
| [Net](../reference/network_CN.md#api-与私有创建) | 私有工厂返回 `std::unique_ptr<http_client_module>`；应用使用 `<ark/net/http_client_module.hpp>` 与类型化查找 | 启用 update；旧 `http_client_module_impl.hpp` 从公开／安装接口移除 |
| [Kernel](../reference/kernel_CN.md#kernel-类型化构造) | 保留公开 `*_impl`、`kernel_plugin` 类型与头路径；私有工厂头不安装 | 公开类布局改变；接口键、update 选择和注册顺序不变 |
| [Consul](../reference/directory-structure_CN.md#consul-类型化构造) | 保留全部三个公开头与类型；私有工厂不安装 | 公开实现／插件布局改变；接口 RTTI 键、禁用 update 不变；链接说明测试入口与依赖条件 |
| [Etcd Client](../reference/directory-structure_CN.md#etcd-client-类型化构造) | 保留全部五个公开头与类型；类型化基类、返回接口的私有工厂与显式入口／出口 | 公开实现／插件布局改变；接口 RTTI 键、禁用 update 不变；链接说明可选 JSON 依赖与无需 Etcd 服务的测试 |
| [OSS SDK](../reference/directory-structure_CN.md#oss-sdk-类型化构造) | 保留全部三个公开头／类型；类型化基类和返回接口的私有工厂 | 公开布局改变；接口 RTTI 键、禁用 update 不变；类型化注销替代未定义的旧调用；仍为 TODO 骨架；链接说明构建与测试边界 |
| [Kafka](../reference/directory-structure_CN.md#kafka-类型化构造) | 保留四个公开头、两个 RdKafka 回调基类；类型化构造与显式入口 | 公开布局改变；接口键、启用逐帧 update 不变；运行时插件默认关闭；链接说明驱动要求与无需 broker 连接的测试 |

## 日志对象与单例生命周期

| 操作 | 所有权与生命周期 |
| --- | --- |
| `logger::init(log)` | 借用宿主日志对象；创建方在插件仍可能写日志时保持对象存活，并负责销毁 |
| `logger::init(nullptr)` | 延迟创建单例；重复调用保留当前 logger |
| `singleton<T>::set_instance(pointer)` | 只切换当前指针，不接管所有权；延迟创建对象仍持有至 `shutdown()`，包括临时借用替换期间 |
| 借用替换 | 销毁借用对象前恢复保存指针，或 `set_instance(nullptr)` 解除关联；不得删除内部自有对象或在停机后恢复其旧指针 |
| `shutdown()` | 清空当前指针，只销毁内部自有对象一次，并阻止延迟创建（包括首次 `instance()` 前）；无进程退出自动清理 |
| 停机后访问 | `instance()` 返回空，除非显式注入；`get_instance()` 要求当前实例有效 |
| `set_instance(nullptr)` | 解除关联，但不阻止尚未执行的延迟初始化 |
| 失败／并发 | 构造异常传播并允许重试；分配失败仍返回空。首次延迟访问同步；注入／停机须与访问串行协调 |

开启 `ARK_BUILD_CORE_TESTS=ON` 后，`check-core` 包含 `check-singleton`，在独立
进程中运行所有权、注入、构造重试、并发初始化、停机和日志对象用例。30 秒进程保护
在挂起时报告失败，并输出已刷新的当前用例及最近完成的断言；异常重试用例还会在进入
构造函数前标记每次调用。运行前还会在独立进程中执行 5 秒有界的 `--probe-call-once`
标准库异常重试对照，记录退出、超时及失败；该诊断不会替代严格的单例测试。
