# 代码规范

本规范用于 ark 的统一整理与后续开发。公开接口、内部实现和文件名统一采用 `snake_case`。

## 命名

| 对象 | 规则 | 示例 |
| --- | --- | --- |
| 命名空间 | 小写，多个单词用下划线连接 | `ark`、`ark::detail` |
| 类、结构体与类型别名 | `snake_case` | `plugin_manager`、`module`、`data_list` |
| 枚举类型与枚举值 | `snake_case` | `data_type::dt_int64` |
| 函数与方法 | `snake_case` | `find_module()`、`start()`、`stop()` |
| 局部变量与参数 | `snake_case` | `module_name`、`initial_buffer_size` |
| 私有与受保护成员 | `snake_case`，末尾加 `_` | `module_name_`、`state_` |
| 公开结构体字段 | `snake_case` | `session_id` |
| 编译期常量 | `snake_case` | `default_buffer_size` |
| 模板参数 | `snake_case` | `module_type`、`buffer_size` |
| 预处理宏 | `UPPER_SNAKE_CASE`，使用 `ARK_` 前缀 | `ARK_EXPORT` |
| C++ 文件 | `snake_case`，头文件用 `.hpp`，实现用 `.cpp` | `plugin_manager.hpp`、`plugin_manager.cpp` |

`ark::plugin_manager` 是统一命名后的公开类型形式。接口和实现通过职责命名；新接口不再使用 `AFI`、`AFC` 类型前缀。依赖库的标识符保留其原有写法。

## 排版与 include 顺序

- 使用 4 个空格缩进，禁止 Tab，列宽为 120。
- `include` 按系统库、第三方库、项目自身头文件分组，组间留一个空行。
- 函数定义的左花括号放在签名同一行；短函数和空函数体均展开书写。
- `if`、`else`、循环的语句体使用花括号，短分支也不省略。
- `include` 与代码、命名空间与内部定义、相邻类型或函数定义之间留空行。
- `clang-format` 负责排版；命名规则在接口审查和相应静态检查中执行。

下面的片段用于演示命名与排版：

```cpp
#include <cstddef>

namespace ark {

class session_counter
{
public:
    void add(std::size_t count) {
        count_ += count;
    }

    std::size_t count() const {
        return count_;
    }

private:
    std::size_t count_ = 0;
};

} // namespace ark
```

## 日志调用位置

`ARK_FORMAT` 和 `ARK_FORMAT_FUNCTION` 使用标准 C++20 `__VA_OPT__` 展开零个或多个
格式参数，继续支持动态格式串、`fmt::runtime` 格式化和字面量花括号。
`ARK_FORMAT` 仍可用于表达式；`ARK_FORMAT_FUNCTION` 保留调用处的
`[function:line]` 前缀及原有的语句结束分号。现有调用方式不变。

`ARK_LOG_TRACE`、`ARK_LOG_DEBUG`、`ARK_LOG_INFO`、`ARK_LOG_WARN`、`ARK_LOG_ERROR`
和 `ARK_LOG_CRITICAL` 保留原有格式串／参数调用方式与日志级别。宏通过 C++20
`std::source_location` 捕获调用处的位置，通过 `__VA_OPT__` 展开可选格式参数。
输出保留 `[basename:line]` 前缀；文件名提取同时识别 `/` 和 `\`，源函数名仍为空。
格式化、参数求值、消息所有权、日志配置和生命周期保持不变。

MSVC 需要启用标准预处理器模式（`/Zc:preprocessor`）才能使用 `__VA_OPT__`。公开的 `ark::core` 目标会向使用者传递该选项；绕过该目标直接编译 ark 头文件时，需要同时启用 C++20 和此选项。

**源码兼容性说明：** ark 不再定义内部辅助宏 `SPDLOG_STRRCHR` 和
`SPDLOG_FILE_BASENAME`，也不会重定义或取消外部已提供的同名宏。但预先定义
`SPDLOG_FILE_BASENAME` 不再影响 `ARK_LOG_*` 的文件名。依赖旧辅助宏定义的代码
需要自行提供文件名提取操作；若需同时识别两种分隔符，可使用
`std::string_view::find_last_of("/\\")`。普通 `ARK_LOG_*` 调用无需修改。

## 实现约定

- 使用 C++20，依赖通过目标声明；公开头文件显式包含其使用的标准库头文件。
- 用 RAII 和清晰的所有权管理资源；借用的指针、引用和视图需要明确有效期。
- 使用 `override`、`final` 和 `const` 表达接口约束。
- 注释使用英文，说明约束、原因和非显然行为。公共 API 写清生命周期、线程和错误处理契约。
- 格式修改与行为修改分别审查；迁移一个模块时同步更新其调用点、示例和测试。

## 本地检查与生成代码

使用 clang-format 18 与仓库中的配置：

```sh
./scripts/format.sh
./scripts/format.sh --check
```

语义命名检查需要先配置编译数据库，再运行 clang-tidy（既有验证使用版本 22）：

```sh
cmake -S . -B style-build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build style-build
./scripts/clang_tidy.sh style-build
./scripts/clang_tidy.sh style-build src/application/main.cpp
```

clang-tidy 默认检查编译数据库中实际启用的源文件；检查可选插件前，需要启用对应
构建选项。插件入口符号 `DllEntryPlugin`、`DllExitPlugin` 与第三方 API、反射字符串
保留必要拼写。

clang-tidy 需要已配置构建的编译数据库及兼容的编译器工具链。通过 `CLANG_FORMAT`、`CLANG_TIDY` 选择非默认可执行文件。格式检查覆盖已跟踪和新增的 ark 自有 C++ 与 Protobuf 源码；第三方实现和构建生成物保留各自规范。

协议定义使用 `snake_case` 与 `ark::protobuf` C++ 命名空间；`consul`、`osslog` 保留既有 package。生成器的 `.pb.h` 后缀不变。配置键、反射字符串、外部 JSON 名称及 C# 生成代码面向业务的名称保留必要拼写。二进制与源码兼容边界见 [Protobuf 消息](../reference/protobuf_CN.md)。

修改 `.proto` 源文件，通过 CMake 重新生成 C++ 输出。生成头文件使用
`<ark/proto/common_messages.pb.h>` 等路径。命名迁移保留消息字段编号、字段类型、
重复规则、枚举数值和对外 JSON 字段名；生成的 C++ 名称、描述符名称、`Any` 类型 URL
和 JSON 枚举文字会改变。使用方需要重新编译，并调整依赖这些名称的代码。
`tests/core/protobuf_messages.cpp` 保留固定字节回归，验证二进制消息兼容性。

配置生成器遵循相同的 C++ 命名规则，在复制到 `include/ark` 前格式化
`meta_define.hpp`。配置数据、反射字符串和 C# 输出保留原有名称。

## C++20 兼容层迁移

**源码不兼容变更：** ark 不再定义 `ARK_CPP11_OR_GREATER`、
`ARK_CPP14_OR_GREATER`、`ARK_CPP17_OR_GREATER`、`ARK_NOEXCEPT`、`ARK_CONSTEXPR`、
`ARK_FINAL` 和 `MAKE_BUILDIN_TYPE`。最低语言版本仍为 C++20。

| 移除的名称 | 迁移方式 |
| --- | --- |
| `ARK_CPP11_OR_GREATER`、`ARK_CPP14_OR_GREATER`、`ARK_CPP17_OR_GREATER` | ark 使用方删除过时的条件分支；独立支持其他语言版本的应用自行维护特性检查。 |
| `ARK_NOEXCEPT` | 直接使用 `noexcept`。 |
| `ARK_CONSTEXPR` | 直接使用 `constexpr`。 |
| `ARK_FINAL` | 直接使用 `final`。 |
| `MAKE_BUILDIN_TYPE(T)` | 若需保留自定义类型分类，显式特化 `ark::is_buildin_type<T>`，继续声明原有的 `yes`、`no` 枚举值。 |

移除 C++11 的 `std::make_unique` 回退实现；包含 `<memory>` 后使用标准 C++20 库。
平台导出、断言和所有权宏保持原有行为。

`is_buildin_type` 仍只识别 `uint16_t`、`uint32_t`、`uint64_t`、`int16_t`、
`int32_t` 和 `int64_t` 六种精确类型，以及与它们等同的原生类型别名；不会扩展到
所有整数类型，也不会移除 cv 或引用限定。`yes`、`no` 枚举值和
`check_buildin_type` 的签名、异常契约、参数推导保持不变：const 整数左值可通过
`const T&` 推导出未限定的类型，但显式检查 const 限定的 trait 仍返回 false。
常量类型和值保持不变，包括 UID 位分配和网络超时。

## POD 字符串迭代器契约

`<ark/string_pod.hpp>` 通过 `ark::core` 提供。`pod_string_iterator::operator++(int)` **按值返回**递增前的迭代器；前置 `++it` 仍按引用返回递增后的迭代器。使用 `auto previous = it++;` 保存旧位置，不再绑定非 const 左值引用。保存的迭代器借用原节点，不复制映射值。

**源码不兼容变更，需重建受影响的使用方与插件：** 修改依赖原后置递增引用的绑定或显式函数签名。即使类成员未变，已生成模板实例的调用约定仍可能变化；布局不变不代表二进制兼容。

`get_data()` 保留声明的 `mapped_type*` 返回类型，返回所存映射值的地址：标量映射对应 `std::size_t*`，指针映射对应 `T**`。通过该地址赋值会修改存储值／指针槽位，不转移指针所指对象的所有权。按名称查询的 `pod_string::get_data(key, out)` 保持不变。

递增或调用 `get_key()`／`get_data()` 前，先检查 `it != values.end()`。空范围满足 `begin() == end()`，不要解引用或递增 end。推进一个迭代器不改变已保存的旧位置，但移除对应节点、清空或销毁容器后，不再使用原借用。其余自定义迭代器与 const 迭代接口保持不变。Core 头文件检查实例化标量与指针映射；`check-core` 覆盖后置递增旧值、存储修改、空范围／end 比较，以及现有 `array_map` 查询路径。

## 标准工具迁移

**源码不兼容变更：** 不再定义 `ARK_TO_STRING`、`ARRAY_LENTGH`、
`ARK_SRATIC_ASSERT`、`ARRAY_CLEAR`、`MEMORY_CLEAR` 和 `MEMORY_CLEAR_POINTER`。

| 移除的名称 | 迁移方式 |
| --- | --- |
| `ARK_TO_STRING(value)` | 包含 `<string>`，调用 `std::to_string(value)`。它与原别名调用的标准函数相同，包括 C++20 的浮点格式化行为。 |
| `ARRAY_LENTGH(array)` | 对有界数组，包含 `<iterator>` 并使用 `std::size(array)`。若需要查询类型的首维数组长度，包含 `<type_traits>` 并使用 `std::extent_v<T>`；`std::size` 不接受裸指针。 |
| `ARK_SRATIC_ASSERT` | 直接使用 `static_assert`。 |
| `ARRAY_CLEAR`、`MEMORY_CLEAR`、`MEMORY_CLEAR_POINTER` | 移除未使用的工具，不提供替代别名。根据对象的存储契约选择值初始化、类型明确的赋值或显式字节操作；不存在对所有对象都安全的清零操作。`MEMORY_CLEAR_POINTER` 还包含非法 token `0xx`。 |

现有数据和端点转换仍以相同的参数类型调用 `std::to_string`，数值输出和精度不变。
数组长度调用点保留原有的 `int` 变量、查找顺序、枚举映射和范围处理。
其余分配、所有权、运行时断言、平台、插件注册和字节序宏保持原有行为。

## 休眠调用迁移

**源码不兼容变更：** `ark/macros.hpp` 不再定义 `ARK_SLEEP`。仓库内没有调用者，
原有三个平台定义均未使用。现有标准库休眠、调度及超时保持不变。

外部调用若表示有限、非负的整数毫秒延迟，包含 `<chrono>` 和 `<thread>` 后改为：

```cpp
std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
```

参数表达式只求值一次。确认值可由 `std::chrono::milliseconds::rep` 表示；
不再需要中间乘以 1000。若调用者依赖原平台的整数转换或溢出、Windows `INFINITE`
或零延迟调度语义、POSIX `usleep` 返回值或信号中断，需要单独审查。
`sleep_for` 返回 `void`，不保留这些契约。

相关契约见 [C++ 草案中的 `sleep_for`](https://eel.is/c++draft/thread.thread.this)、
[Windows `Sleep`](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleep)
和 [Apple `usleep` 手册](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/usleep.3.html)。
