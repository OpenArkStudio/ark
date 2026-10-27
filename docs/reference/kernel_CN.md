# Kernel 插件

Kernel 管理运行时实体、实体结构、静态配置和地图实例。业务模块通过 `kernel_module` 创建、查找和销毁实体，通过 `entity` 访问字段、表格和容器。

## 模块与数据模型

| 对象 / 模块 | 用途 | 主要接口 |
| --- | --- | --- |
| `class_meta_module` | 加载实体与配置的结构定义，管理类事件回调 | `find_meta()`、`find_static_meta()`、`add_class_call_back()` |
| `config_module` | 按配置 ID 查找已加载的静态数据 | `find_static_entity()` |
| `map_module` | 创建地图和实例，维护实例内实体列表 | `create_map()`、`create_map_instance()`、`get_inst_entity_list()` |
| `kernel_module` | 管理实体及其事件，转换实体数据 | `create_entity()`、`get_entity()`、`destroy_entity()`、`entity_to_db_data()` |
| `entity` | 运行时数据：字段、表格、容器和空间位置 | `get_int32()`、`set_int32()`、`find_table()`、`find_container()` |
| `static_entity` | 从资源文件加载的配置记录 | `get_config_id()`、`get_int32()`、`get_string()` |

实体结构和配置值是两类数据：结构定义字段名称、类型、索引与属性；静态配置为配置 ID 提供具体值；运行时实体拥有独立的 ID 和可修改数据。

### 资源加载

元数据模块在 `init()` 中加载 `resource/meta/config_class.config` 和 `resource/meta/entity_class.config`。这两个文件引用具体的结构文件；配置结构还可指定数据资源文件。`config_module` 在 `post_init()` 中按元数据加载静态记录。

业务代码应在框架完成相应初始化后查询配置。创建运行时实体前，需要存在对应类结构、地图和地图实例。类事件回调应在目标实体创建前注册。模块接入方式见[创建模块](../guides/modules_CN.md)。

## 创建和查找实体

以下函数用于已加载 Kernel 插件的进程。`map_id` 由应用分配；`create_map()` 会创建编号为 `0` 的默认实例。示例使用已有的 `Player` 结构，不加载配置记录，也不写入业务字段。

```cpp
#include <memory>

#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/kernel_module.hpp"
#include "ark/kernel/map_module.hpp"
#include "ark/meta_define.hpp"
#include "ark/plugin_manager.hpp"

std::shared_ptr<ark::entity> create_player(ark::plugin_manager& manager, int map_id) {
    auto* kernel = manager.find_module<ark::kernel_module>();
    auto* maps = manager.find_module<ark::map_module>();
    if (kernel == nullptr || maps == nullptr)
    {
        return nullptr;
    }

    if (!maps->exist_map(map_id) && !maps->create_map(map_id))
    {
        return nullptr;
    }

    ark::data_list_impl args;
    return kernel->create_entity(ark::null_guid, map_id, 0, ark::entity_meta_player::self_name(), 0, args);
}
```

`create_entity()` 返回 `nullptr` 表示创建失败，例如类结构、地图或实例不存在，实体 ID 重复，或参数数量不符合要求。传入 `null_guid` 时由 Guid 模块生成 ID；初始化参数按「值、字段索引」成对排列；本例传入空参数列表。已有实体可用 `get_entity(id)` 查询，用 `exist(id)` 检查，用 `destroy_entity(id)` 请求销毁。

持有 `shared_ptr<entity>` 不等于实体仍在 Kernel 中注册。销毁后不要继续将外部保存的指针当作有效业务实体；需要时按 ID 重新查询。

## 配置默认值与初始化参数

`config_id > 0` 时，配置记录必须存在且与实体类匹配。配置为未指定的字段提供默认值，合法的 `args` 覆盖同一字段；显式传入 `0` 或空字符串也会覆盖配置值。同一字段在合法参数中重复出现时，最后一个值生效。实体数据独立于配置记录，修改实体不会修改原配置。

参数按 `(value, uint32_t field_index)` 成对排列：先加入值，再用 `add_u_int()` 加入字段索引。索引必须来自已加载的实体结构，值类型必须与字段类型完全一致；不要依赖隐式转换。

下面使用声明为 int32 的 `Player.level` 和 string 的 `Player.name`，显式将等级设为 `0`、名称设为空字符串，其余字段保留配置默认值。调用前，目标地图、实例和对应的 Player 配置记录必须存在。

```cpp
#include <memory>

#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/kernel_module.hpp"
#include "ark/meta_define.hpp"

std::shared_ptr<ark::entity> create_configured_player(
    ark::kernel_module& kernel, int map_id, int instance_id, ark::id_type config_id) {
    ark::data_list_impl args;
    args.add_int(0);
    args.add_u_int(ark::entity_meta_player::level_index());
    args.add_string("");
    args.add_u_int(ark::entity_meta_player::name_index());

    return kernel.create_entity(
        ark::null_guid, map_id, instance_id, ark::entity_meta_player::self_name(), config_id, args);
}
```

配置默认值与参数值在第一个类事件 `entity_evt_pre_load_data` 前就已就绪。`create_container_entity()` 没有 `args` 参数，按配置初始化；`create_entity(protobuf_data)` 从保存的数据恢复，不自动合并配置默认值。

## 访问字段、表格和配置

字段读写属于 `entity`，可以使用字段名称或元数据索引。类型应与结构定义一致；不要用数值返回值判断实体或字段是否存在。

```cpp
#include <cstdint>
#include <string>

#include "ark/kernel/kernel_module.hpp"

bool set_int32_field(ark::kernel_module& kernel, const ark::guid_t& id, const std::string& field, std::int32_t value) {
    auto object = kernel.get_entity(id);
    if (object == nullptr || object->get_node_type(field) != ark::data_type::dt_int32)
    {
        return false;
    }

    return object->set_int32(field, value);
}
```

| 数据类型 | 访问方式 |
| --- | --- |
| 单值字段 | `entity::get_bool()`、`get_int32()`、`get_int64()`、`get_string()`、`get_guid()` 等；写入使用对应的 `set_*()` |
| 表格 | `entity::find_table()` 返回 `table*`；使用 `find_row()` / `add_row()` 获取 `row*`，再按列名称或索引读写 |
| 容器 | `entity::find_container()` 获取容器；`kernel_module::create_container_entity()` 创建容器内实体 |
| 静态配置 | `config_module::find_static_entity(config_id)` 返回 `shared_ptr<static_entity>`；检查空值后按已知字段类型读取 |

`table*` 和 `row*` 是借用指针，不独立拥有实体数据。表格删除行、清空表格或销毁所属实体后，不要继续使用相关指针。

### 自定义无符号值

`entity::add_custom_u_int32(name, value)` 创建按名称访问的运行时 `uint32_t` 值，与元数据字段分开。创建后，`set_custom_u_int32(name, value)` 可在 `0` 到 `UINT32_MAX` 的完整范围内更新值，使用 `get_custom_u_int32(name)` 读取。名称不存在或对应其他标量类型时，设置操作在 Debug 和 Release 构建中均返回 `false`，不会创建数据、转换类型或修改已有值。使用 `find_custom_data()` 检查是否存在；已存储的零是有效值。

`check-kernel-initialization` 通过公开的 `entity` 接口验证这些操作，并检查其他自定义标量。启用应用、Kernel、Utility 和示例后，`check-typed-examples` 还会在真实 Kernel、EntityOperations 动态库创建的实体上验证无符号值更新。

## 注册事件

可以按类注册创建 / 销毁事件、字段变化、表格变化和容器操作，也可以为单个实体注册业务事件。公共类事件通过 `add_common_class_event()` 注册。

```cpp
#include <string>

#include "ark/kernel/kernel_module.hpp"

class entity_observer
{
public:
    bool attach(ark::kernel_module& kernel, const std::string& class_name) {
        return kernel.add_class_call_back(class_name, this, &entity_observer::on_class_event);
    }

private:
    int on_class_event(
        const ark::guid_t& id, const std::string& class_name, ark::entity_event event, const ark::data_list& args) {
        if (event == ark::entity_event::entity_evt_data_finished)
        {
            // Handle the entity after its creation stages complete.
        }
        return 0;
    }
};
```

注册保存了对象指针；`entity_observer` 必须在回调可能执行期间一直有效。不要使用创建后立即离开作用域的局部对象注册成员回调。当前接口未提供对应的公开注销方法。

普通 `create_entity()` 的类事件按以下顺序同步分发：

| 阶段 | `entity_event` |
| --- | --- |
| 加载前 | `entity_evt_pre_load_data` |
| 加载 | `entity_evt_load_data` |
| 应用数据前 | `entity_evt_pre_effect_data` |
| 应用数据 | `entity_evt_effect_data` |
| 应用数据后 | `entity_evt_post_effect_data` |
| 数据阶段完成 | `entity_evt_data_finished` |

`entity_evt_all_finished` 由应用按需显式触发，不是上述创建函数自动发送的阶段。正常实体销毁时先分发 `entity_evt_pre_destroy`，再分发 `entity_evt_destroy`。

字段和表格回调分别使用 `add_node_call_back()`、`add_table_call_back()`；参数包含实体 ID、变化位置以及旧值 / 新值。`do_event(id, event_id, args)` 用于分发单实体业务事件。回调参数和优先级以公开头文件签名为准。

## 地图与数据转换

地图按 `map_id` 区分，每张地图可以创建多个实例。`create_map_instance()` 成功时返回实例 ID，失败返回 `-1`。`switch_map()` 将已有实体移到目标实例；目标地图和实例必须事先存在。

地图模块提供实体列表与在线数量查询。它不提供完整的可见范围管理（AOI）；九宫格、空间索引和业务广播范围需要由应用实现。`get_max_online_count()` 当前是固定返回值，不应作为部署容量上限或容量测试结论。

`entity_to_db_data()` 将实体转换为 `ark::protobuf::protobuf_db_entity`，对应的 `create_entity(protobuf_data)` 重载用于恢复。`entity_node_to_protobuf_data()` 和 `entity_table_to_protobuf_data()` 可按数据掩码转换部分字段。转换不等于写入数据库；持久化、版本兼容和恢复后的业务校验由应用负责。

## API 来源

- [Kernel 接口](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/kernel/kernel_module.hpp)
- [Entity 接口](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/kernel/entity.hpp)
- [元数据接口](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/kernel/class_meta_module.hpp)
- [配置接口](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/kernel/config_module.hpp)
- [地图接口](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/kernel/map_module.hpp)
- [数据定义与生成](../guides/data-structures_CN.md)

## Kernel 源码共置

Kernel 的原有 38 个头与 cpp 共同位于 `src/plugins/kernel/`。
`ark::kernel` 的显式映射保留全部 `<ark/kernel/...>` 包含路径，
包括 `data_impl.hpp`、`data_list_impl.hpp` 和 `kernel_plugin.hpp`，运行时插件关闭时同样可用。
业务代码仍可使用原有值类型和模板定义。四个模块实现现在继承
`module_base<原接口>`，`kernel_plugin` 继承 `plugin_base<kernel_plugin>`。
核心源码头位于 `src/core/`。

独立工程 `tests/consumer/kernel` 仅链接 `ark::kernel`，开启 Protobuf 时逐个编译
38 个头，并运行值类型和模板消费检查。`container_meta.hpp` 保留原有的
`ark/define.hpp`、`ark/enum.hpp` 前置包含要求，消费者显式提供这两个头。`ARK_BUILD_PROTO=OFF` 时检查其余 35 个头；
`kernel_module.hpp`、`kernel_module_impl.hpp`、`map_module_impl.hpp`
仍要求 Protobuf。数据与节点、初始化与配置、应用回归分别使用 `check-core`、
`check-kernel-initialization`、`check-typed-examples`。
`check-kernel-initialization` 要求 `ARK_BUILD_PROTO=ON`；`check-typed-examples`
还要求开启应用、配置、示例构建及 Kernel/Utility 运行时目标。

```sh
cmake -S tests/consumer/kernel -B kernel-headers-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_FEATURES=application -DASIO_STANDALONE=OFF
cmake --build kernel-headers-build --target check-kernel-headers --parallel 2
```

## Kernel 类型化构造

**ABI／布局不兼容变更：** `class_meta_module_impl`、`config_module_impl`、`kernel_module_impl`、`map_module_impl` 现在继承 `module_base<原接口>`，`kernel_plugin` 继承 `plugin_base<kernel_plugin>`。原有公开类型、构造函数、方法和 38 个头路径继续保留。Kernel 与消费者必须一起重建；旧实现类／插件二进制不兼容变更后的类布局。此变更不引入跨编译器的插件 ABI。

插件使用私有拥有型工厂与显式 `DllEntryPlugin`／`DllExitPlugin`。`kernel_factory.hpp` 仅保留在插件源码目录，不映射、不安装。工厂返回类型保留原接口 RTTI 注册键：

| 注册接口 | 保留的实现类型 | update 注册 |
| --- | --- | --- |
| `class_meta_module` | `class_meta_module_impl` | `module_update::disabled`，继承默认 update |
| `config_module` | `config_module_impl` | `module_update::disabled`，继承默认 update |
| `kernel_module` | `kernel_module_impl` | `module_update::enabled`，保留原有覆写 |
| `map_module` | `map_module_impl` | `module_update::disabled`，继承默认 update |

安装按表格顺序执行，卸载按相反顺序执行。所有注册仍在生命周期初始化前完成，初始化阶段的容器遍历不保证依赖顺序。实体默认值、配置／结构加载阶段、Protobuf 消息及业务数据格式不变。

开启 Protobuf 且存在 Kernel／Utility 运行时 target 时，`check-kernel-plugin` 成为 `check-core` 的依赖。独立 runner 仅链接公开 `ark::kernel`，加载两个真实 DSO，检查四个接口注册键与管理器归属、元数据／静态记录加载、地图／实体注册、公开生命周期回调、管理器更新、正常插件退出和重复 stop。不在卸载后检查注册表，也不证明任意热更新安全。公开头消费者还检查类型基类及原有 update 覆写；现有初始化和实体默认值回归继续保留。

**无符号 64 位 getter 迁移：**`static_entity::get_u_int64()` 及 `static_entity_impl` 的两个覆写现在返回 `uint64_t`，保留到 `UINT64_MAX` 的完整范围。将自定义覆写和误用 `int64_t` 的接收变量改为无符号类型，并一起重新构建 Kernel 与 C++ 消费者。有符号字段继续使用 `get_int64()`。
