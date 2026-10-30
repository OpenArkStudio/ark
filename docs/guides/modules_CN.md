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

## 声明启动依赖

用普通 C++ 模板参数声明必需 provider。Timers 示例采用相同形式：

```cpp
#include <ark/module_base.hpp>
#include <ark/utility/guid_module.hpp>
#include <ark/utility/timer_module.hpp>

class dependent_module final
    : public ark::module_base<ark::module, ark::timer_module, ark::guid_module>
{
};
```

`module_base<interface_type, providers...>::get_dependencies()` 返回 provider 接口的 RTTI 注册键。第一个参数仍选择继承的接口；provider 只增加元数据，不增加基类或自有指针。`module::get_dependencies()` 默认返回空列表。旧实现也可覆写它，直接返回必需注册键。读取元数据时不得修改 registry；管理器在安装完成后于 owner 线程读取一次并缓存已验证顺序。合法 idle 模块新增／移除使缓存失效，首次停止接收阶段会按需重建一次。声明在注册期间应保持稳定；轮询和重入 stop 不会重复读取。重复键只计算一次，自依赖视为环。可选依赖继续使用可空查询，不应声明为必需。验证检查注册键是否存在；直接调用 `add_module()` 的代码仍须保证对象实现了所注册的接口。

本阶段迁移的小应用为 Timers 加 Utility。Timers 在 `init()` 中实际查找 Timer 和 Guid；Utility 的 Timer、Guid、Schedule 和 Compress 实现没有必需的模块 provider。其他应用保留已有声明和查询检查，尚未形成完整的回调生命周期图。稳定注册键顺序只在兼容构建内定义，不保证不同编译器的 RTTI 编码具有相同排序。

**升级要求：** `module` 新增元数据与 `stop_accepting()` 虚方法，虚表发生变化；管理器缓存生命周期顺序，对象布局也改变。Timers 的公开基类特化同样改变。必须使用兼容的头文件／编译器配置，同时重建启动器、全部官方／业务插件及受影响的消费者，不得混用旧二进制。已有查找键保持不变，此 C++ 边界不提供跨编译器 ABI 保证。元数据控制启动，以及反向的停止接收钩子顺序。update、就绪检查、`pre_shut()`、`shut()` 和插件卸载保留原有顺序。官方入口迁移、按依赖反向完整清理和可选的启动失败补偿，仍属于[已选生命周期路线](../project/lifecycle-closeout_CN.md)的后续阶段。

## 选择生命周期阶段

| 回调 | 适合处理的工作 | 当前约束 |
| --- | --- | --- |
| `init()` | 初始化本模块资源 | 声明的 provider 已完成同一阶段，独立模块可能稍后执行 |
| `post_init()` | 查找接口，使用已在 `init()` 中初始化的依赖 | 声明的 provider 也先于消费者完成 `post_init()` |
| `check_config()` | 检查配置和依赖 | 失败时停止剩余启动工作 |
| `pre_update()` | 准备进入应用循环 | 避免在这个阶段执行无期限的工作 |
| `update()` | 分发服务与桥接的完成结果 | 只调用实现类覆盖的 `update()`，随后管理器统一轮询一次共享执行器；布尔返回值不会停止更新 |
| `stop_accepting()` | 关闭外部业务根请求入口 | 排空任务前按消费者优先顺序执行一次；false／异常保留资源 |
| `pre_shut()` | 解除订阅，准备清理资源 | 业务任务完成且更新/完成回调返回后才进入；首次返回 `false` 或抛出异常时停止关闭流程 |
| `shut()` | 释放模块持有的资源 | 只有全部 `pre_shut()` 成功才进入；首次返回 `false` 或抛出异常后，不再执行剩余回调或继续卸载 |

![模块成功启动的生命周期顺序](../assets/module-lifecycle.svg)

管理器先安装所有模块，再验证声明的 provider 注册键并生成启动顺序。`init()`、`post_init()`、`check_config()` 和 `pre_update()` 使用同一顺序，先 provider、后消费者。没有声明依赖的模块视为独立节点；当前可启动节点之间按注册键的字典序选择，不按清单或插入顺序执行。基类回调默认返回 `true`，只需覆盖使用到的阶段。注册宏检测到实现类覆盖 `update()` 时，才把它加入更新列表。因此计数器实现不参与逐帧更新，`sample_module` 则在每次管理器更新时递增一次计数器。

正常停止时，管理器先关闭顶层业务任务准入，再执行一次性停止接收钩子，然后继续驱动已受理任务，不会关闭它们使用的服务或桥接准入。任务排空后，关闭钩子与卸载同步执行；钩子可能阻塞，轮询的 handler 数量上限也不限制其耗时。钩子失败时，启动器输出阶段和模块信息，保留进程及资源，不会自动重试这些钩子。

<Aside type="caution" title="检查管理器的启动结果">

缺少必需 provider 注册键或依赖成环时，`start()` 在调用任何模块启动钩子前返回 false。`get_startup_failure()` 保留 `dependencies: <consumer> requires missing module '<provider>'`，或列出被依赖环阻塞的模块；阻塞列表也可能包含环外的下游消费者。元数据异常保持原样向上传播，并保留 `dependencies: <module>` 上下文。此时注册和安装的副作用已发生，验证不会回滚这些资源。

`init()`、`post_init()`、`check_config()` 或 `pre_update()` 首次返回 `false` 时，管理器停止该阶段的剩余模块及后续启动阶段，`plugin_manager::start()` 返回 `false`。同一阶段内每个回调至多执行一次。异常保持原样向外传播，也会停止剩余启动调用。失败时，`get_startup_failure()` 保留当前的 `phase: module` 上下文；库加载及其入口调用使用 `load_plugin: plugin`。成功启动后上下文为空，读取时必须位于管理器所属线程。已经完成的阶段不会自动回滚。

应用启动器输出保留的上下文及可用异常信息，并在进入更新循环或启动 Windows 命令线程前返回退出码 `1`。这个失败路径不会调用 `pre_shut()` 或 `shut()` 清理部分启动资源，也不会在同一进程中重试；部署工具应启动新进程。缺少库文件或 `DllEntryPlugin` 入口时，管理器返回 false，并保留 `load_plugin: <plugin>` 上下文。已加载但缺少入口的库保留至进程退出，因为其静态初始化可能已启动工作；管理器不会自动卸载或回滚。其他程序不变量的断言保持不变。`update()` 的返回值仍被忽略。部署时应完整配置依赖插件；自行管理循环的调用方需要检查启动结果。

</Aside>

## 停止接收新业务根请求

覆写 `bool stop_accepting()`，只关闭模块自己的外部业务根请求入口。默认实现返回 `true`，保持源码兼容。已接收流程仍需访问 provider 请求、完成通知和 bridge；不得因为名称类似，就在这里关闭 provider 的 `close()`、`stop()` 或操作准入。

首次 `manager.stop()` 关闭全部受管任务组的顶层准入，进入 `stopping_acceptance`。如果 update、poll 或完成回调仍在执行，则返回 false，等外层 owner frame 再调用 `stop()` 后执行模块钩子。钩子按缓存启动拓扑的反向顺序运行，**先消费者、后 provider**，然后才等待受管任务完成；独立节点的顺序也是启动键顺序的反向。启动时缓存已验证顺序；合法 idle 模块新增／移除使缓存失效。从未调用 start 的手动管理器，或缓存已失效的管理器，会在任何钩子执行前、生命周期 guard 保护下重建并验证一次当前图，包括同键替换，不使用失效缓存中的旧指针。依赖声明在注册生存期内应保持稳定；轮询期间和钩子序列开始后，不再读取元数据。

每个实际到达的钩子只调用一次。`true` 允许继续下一个钩子；`false` 是硬失败，不表示等待就绪。首个 false／异常或无效重建图使状态锁定为 `failed`，保留 `stop_accepting: <module>` 上下文与异常详情，不再执行后续钩子、更新或清理。已经关闭的入口不会重新打开，重复 stop 不重试，已接收帧和资源继续保留。钩子可以阻塞，协作式超时不能中断它。重入 stop 返回 false；递归 update／start 和 registry 修改在生效前被拒绝。

全部钩子成功后进入 `draining`：继续启用的模块更新和共享 owner poll，直到已接收任务及活跃栈结束，再通过现有的全部模块就绪检查并清理。本阶段不改变 `pre_shut()`、`shut()`、插件出口或库关闭顺序，也不增加启动失败补偿。

通过 `stop_until(timestamp, deadline)` 传入截止时间后，管理器在停止接收／就绪钩子返回后及清理前检查时间；后续 `stop()` 不能绕过超时保留。清理前的 `timed_out` 保留资源并允许更新；清理开始后的 `cleanup_timed_out` 保留剩余资源并停止更新。检查不能抢占钩子，也不能回滚已经执行的动作。详见[超时状态](tasks_CN.md#模块共用业务执行器)。

**迁移边界：** 官方 HTTP 服务端按请求停止接收并排空已接收响应。Net 提供可选的[显式业务根回调助手](../reference/network_CN.md#显式注册外部业务根)，要求应用提供拒绝处理器。原普通注册和访客房间行为不变；其余 TCP／WebSocket／RUDP 业务入口须逐项迁移。业务回调入口的接纳检查不暂停传输层 accept，直接停止传输或操作 acceptor 不能代替已接收流程的保留。参见 [HTTP 停服](../reference/http_CN.md#路由请求)。

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
manager/name 访问函数、必需 provider 元数据及受保护的 `find_module<T>()`，不覆写启动／停机钩子或 `update()`。
查找仍要求目标接口已经注册。在实现中包含基类即可，调用方无需依赖具体类。

私有工厂返回 `std::unique_ptr<interface_type>`。
[插件的类型化注册辅助函数](plugins_CN.md#用类型化注册和显式入口)
按该接口注册，并要求显式选择是否参与 update。原有模块宏仍可使用。
