# 创建模块

模块通过 C++ 接口提供应用服务，插件负责创建和注册实现，插件管理器调用生命周期回调。实体访问、定时器和业务规则都可以作为模块；插件用于组合并安装一组模块。

下面将计数器接口与实现分开，并由另一个模块调用。配合[创建插件](plugins_CN.md)中的示例即可构建动态库。

## 定义接口与实现

将下面的代码保存为 `sample_module.hpp`：

```cpp
#pragma once

#include <cstdint>

#include <ark/module.hpp>
#include <ark/plugin_manager.hpp>

namespace ark {

class counter_module : public module
{
public:
    virtual void increment() = 0;
    virtual std::uint64_t value() const = 0;
};

class counter_module_impl final : public counter_module
{
    ARK_DECLARE_MODULE_FUNCTIONS

public:
    void increment() override {
        ++value_;
    }

    std::uint64_t value() const override {
        return value_;
    }

private:
    std::uint64_t value_ = 0;
};

class sample_module final : public module
{
    ARK_DECLARE_MODULE_FUNCTIONS

public:
    bool post_init() override {
        counter_module_ = find_module<counter_module>();
        return counter_module_ != nullptr;
    }

    bool update() override {
        if (counter_module_ == nullptr)
        {
            return false;
        }

        counter_module_->increment();
        return true;
    }

    bool pre_shut() override {
        counter_module_ = nullptr;
        return true;
    }

private:
    counter_module* counter_module_ = nullptr;
};

} // namespace ark
```

示例把三个类型放在同一个文件中，便于说明关系。应用项目应向调用方提供接口头文件，将实现细节保留在内部。两个实现类都使用 `ARK_DECLARE_MODULE_FUNCTIONS`；这个宏保存管理器指针和注册名称，并提供受保护的 `find_module<T>()` 方法。

## 使用同一个接口注册和查找

以 `counter_module` 为接口注册 `counter_module_impl`，再通过 `find_module<counter_module>()` 查找。注册键来自接口类型的 RTTI 名称；实现类名称和动态库文件名都是不同的键。同一个管理器中，每个接口只注册一个实现。注册宏不会把重复注册转换成受支持的多实现模型。

返回的指针由插件持有，调用方不拥有对象。不要删除这个指针，也不要在插件注销模块后继续使用。只要管理器仍有业务任务，任何模块都不能注销，因为其他模块的任务可能仍引用该服务提供方。接口不存在或动态类型转换失败时，启用断言的构建会触发断言；关闭断言时，`find_module<T>()` 返回 `nullptr`。使用前应检查返回值，不要在 Debug 中通过失败查找探测可选服务。应用和插件需要使用兼容的头文件、编译器与 ABI 设置，并启用 RTTI。

`plugin_manager::try_find_module<T>()` 用于允许依赖缺失的查询。接口 RTTI 键不存在，或已注册对象的运行时类型不匹配时，返回 `nullptr`，不记录日志、触发断言或修改注册。只能在管理器所属线程调用；其他线程调用时抛出 `std::logic_error`。现有 `find_module<T>()` 保留断言契约。服务启动路径使用可空查询，将缺少必需模块的情况传递到正常启动失败处理。

示例中的两个模块在应用更新线程中调用。计数器没有使用原子变量，不支持任意工作线程直接访问。工作线程的完成结果应投递到模块所属线程，或使用服务明确提供的异步 API。网络回调的分发方式见[网络接口](../reference/network_CN.md)。

## 选择生命周期阶段

| 回调 | 适合处理的工作 | 当前约束 |
| --- | --- | --- |
| `init()` | 初始化本模块资源 | 其他已注册模块可能尚未执行 `init()` |
| `post_init()` | 查找接口，使用已在 `init()` 中初始化的依赖 | 其他模块的 `post_init()` 不按依赖顺序执行 |
| `check_config()` | 检查配置和依赖 | 失败时停止剩余启动工作 |
| `pre_update()` | 准备进入应用循环 | 避免在这个阶段执行无期限的工作 |
| `update()` | 分发服务与桥接的完成结果 | 只调用实现类覆盖的 `update()`，随后管理器统一轮询一次共享执行器；布尔返回值不会停止更新 |
| `pre_shut()` | 解除订阅，准备清理资源 | 业务任务完成且更新/完成回调返回后才进入；首次返回 `false` 或抛出异常时停止关闭流程 |
| `shut()` | 释放模块持有的资源 | 只有全部 `pre_shut()` 成功才进入；首次返回 `false` 或抛出异常后，不再执行剩余回调或继续卸载 |

![模块成功启动的生命周期顺序](../assets/module-lifecycle.svg)

管理器先安装所有模块，再执行初始化阶段。每个阶段遍历已注册模块；当前容器不提供依赖排序，也不保证按照清单顺序执行。基类回调默认返回 `true`，只需覆盖使用到的阶段。注册宏检测到实现类覆盖 `update()` 时，才把它加入更新列表。因此计数器实现不参与逐帧更新，`sample_module` 则在每次管理器更新时递增一次计数器。

正常停止时，管理器先停止受理新的顶层业务任务，继续驱动已受理任务，不会关闭它们使用的服务或桥接准入。任务排空后，关闭钩子与卸载同步执行；钩子可能阻塞，轮询的 handler 数量上限也不限制其耗时。钩子失败时，启动器输出阶段和模块信息，保留进程及资源，不会自动重试这些钩子。

<Aside type="caution" title="检查管理器的启动结果">

`init()`、`post_init()`、`check_config()` 或 `pre_update()` 首次返回 `false` 时，管理器停止该阶段的剩余模块及后续启动阶段，`plugin_manager::start()` 返回 `false`。同一阶段内每个回调至多执行一次。异常保持原样向外传播，也会停止剩余启动调用。失败时，`get_startup_failure()` 保留当前的 `phase: module` 上下文；库加载及其入口调用使用 `load_plugin: plugin`。成功启动后上下文为空，读取时必须位于管理器所属线程。已经完成的阶段不会自动回滚。

应用启动器输出保留的上下文及可用异常信息，并在进入更新循环或启动 Windows 命令线程前返回退出码 `1`。这个失败路径不会调用 `pre_shut()` 或 `shut()` 清理部分启动资源，也不会在同一进程中重试；部署工具应启动新进程。缺少库文件或 `DllEntryPlugin` 入口时，管理器返回 false，并保留 `load_plugin: <plugin>` 上下文。已加载但缺少入口的库保留至进程退出，因为其静态初始化可能已启动工作；管理器不会自动卸载或回滚。其他程序不变量的断言保持不变。`update()` 的返回值仍被忽略。部署时应完整配置依赖插件；自行管理循环的调用方需要检查启动结果。

</Aside>

## 保证回调不超出对象生命周期

示例只保存其他模块的指针，并在 `pre_shut()` 中清空。实际模块还可能注册定时器、事件处理器或捕获 `this` 的异步操作。保存这些操作的标识符或取消句柄，先停止接收新任务，再按所属服务的 API 注销或取消，最后才销毁模块。不能仅凭取消操作推断所有完成回调都已执行。

管理器追踪自己任务组中受理的业务任务，不追踪任意外部回调、原生连接池线程或后台清理。这些操作仍需按所属服务的约定完成，才能释放相关代码或状态。不能根据受管理任务数归零、`pre_shut()`、`shut()` 或 `stop()` 返回成功，推断动态库可以安全替换，或所有回调都已排空。[模块共享任务](tasks_CN.md#模块共用业务执行器)说明了任务准入与排空约定。热更新不在本文范围内。

## 继续阅读

- [创建插件](plugins_CN.md)：配对注册、动态库入口、构建与运行选择。
- [应用启动器](applications_CN.md)：进程参数和配置路径。
- [协程任务](tasks_CN.md)：显式任务所有权、准入和所属线程轮询。
- [Kernel 接口](../reference/kernel_CN.md)：官方模块提供的实体与数据服务。

## 用基类隐藏具体实现

新模块可用 `<ark/module_base.hpp>` 中的 `ark::module_base<interface_type>` 替代
`ARK_DECLARE_MODULE_FUNCTIONS`。接口头文件只需包含 `<ark/module.hpp>`，具体类放在
实现 `.cpp` 中：

```cpp
// counter_factory.hpp：插件私有的工厂声明
#include <memory>
#include "counter_module.hpp" // 声明 ark::counter_module

std::unique_ptr<ark::counter_module> make_counter();

// counter_module.cpp
#include <ark/module_base.hpp>

class counter_impl final : public ark::module_base<ark::counter_module>
{
public:
    void increment() override { ++value_; }
    std::uint64_t value() const override { return value_; }
private:
    std::uint64_t value_{};
};

std::unique_ptr<ark::counter_module> make_counter() {
    return std::make_unique<counter_impl>();
}
```

单继承链为具体实现 → `module_base<interface_type>` → 接口 → `module`。基类实现
manager/name 访问函数及受保护的 `find_module<T>()`，不覆写生命周期或 `update()`。
查找仍要求目标接口已经注册。在实现中包含基类即可，调用方无需依赖具体类。

私有工厂返回 `std::unique_ptr<interface_type>`。
[插件的类型化注册辅助函数](plugins_CN.md#用类型化注册和显式入口)
按该接口注册，并要求显式选择是否参与 update。原有模块宏仍可使用。
