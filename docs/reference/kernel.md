# Kernel plugin

Kernel manages runtime entities, entity schemas, static configuration and map instances. Application modules use `kernel_module` to create, find and destroy entities, then use `entity` to access fields, tables and containers.

## Modules and data model

| Object / module | Responsibility | Main interfaces |
| --- | --- | --- |
| `class_meta_module` | Load entity and configuration schemas; manage class event callbacks | `find_meta()`, `find_static_meta()`, `add_class_call_back()` |
| `config_module` | Find loaded static data by configuration ID | `find_static_entity()` |
| `map_module` | Create maps and instances; track entities in each instance | `create_map()`, `create_map_instance()`, `get_inst_entity_list()` |
| `kernel_module` | Manage entities and events; convert entity data | `create_entity()`, `get_entity()`, `destroy_entity()`, `entity_to_db_data()` |
| `entity` | Runtime fields, tables, containers and spatial position | `get_int32()`, `set_int32()`, `find_table()`, `find_container()` |
| `static_entity` | A configuration record loaded from resource files | `get_config_id()`, `get_int32()`, `get_string()` |

Schemas and configuration values are separate data: a schema defines field names, types, indices and attributes; static configuration supplies values for a configuration ID; runtime entities have their own IDs and mutable data.

### Resource loading

The metadata module loads `resource/meta/config_class.config` and `resource/meta/entity_class.config` during `init()`. These files reference individual schema files; configuration schemas may also specify data resources. During `post_init()`, `config_module` loads static records using this metadata.

Query configuration after the framework has completed the relevant initialization. Creating a runtime entity requires its class schema, map and map instance to exist. Register class event callbacks before creating entities of that class. See [Create a module](../guides/modules.md) for module integration.

## Create and find entities

The following function runs in a process with the Kernel plugin loaded. The application chooses `map_id`; `create_map()` creates its default instance with ID `0`. This example uses the existing `Player` schema, with no configuration record or application field writes.

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

`create_entity()` returns `nullptr` on failure, for example when the class schema, map or instance is missing, the entity ID already exists, or the argument count is invalid. Passing `null_guid` requests an ID from the Guid module. Initialization arguments are value/field-index pairs; this example passes an empty list. Use `get_entity(id)` to find an entity, `exist(id)` to test registration and `destroy_entity(id)` to request destruction.

Holding a `shared_ptr<entity>` does not mean the entity remains registered with Kernel. After destruction, do not treat an externally retained pointer as a live application entity; look it up by ID again when needed.

## Configuration defaults and initialization arguments

When `config_id > 0`, the configuration record must exist and match the entity class. Configuration supplies defaults for unspecified fields; valid `args` override the same field, including an explicit `0` or empty string. If a field appears more than once in valid arguments, the last value wins. Entity data is independent of the configuration record, so modifying an entity does not modify the source configuration.

Arguments are `(value, uint32_t field_index)` pairs: add the value first, then add its index with `add_u_int()`. Indices must belong to the loaded entity schema, and each value type must exactly match its field type. Do not rely on implicit conversions.

The following example uses `Player.level` declared as int32 and `Player.name` declared as string. It explicitly sets the level to `0` and the name to an empty string, while retaining configuration defaults for the other fields. The target map, instance and matching Player configuration record must already exist.

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

Defaults and argument values are ready before the first class event, `entity_evt_pre_load_data`. `create_container_entity()` has no `args` parameter and initializes from configuration. `create_entity(protobuf_data)` restores saved data without automatically merging configuration defaults.

## Access fields, tables and configuration

Field access belongs to `entity`, using a field name or metadata index. The accessor type must match the schema. Do not use a numeric field value to determine whether an entity or field exists.

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

| Data | Access |
| --- | --- |
| Scalar fields | `entity::get_bool()`, `get_int32()`, `get_int64()`, `get_string()`, `get_guid()` and related methods; write using the matching `set_*()` |
| Tables | `entity::find_table()` returns `table*`; use `find_row()` / `add_row()` to obtain `row*`, then access columns by name or index |
| Containers | `entity::find_container()` returns a container; `kernel_module::create_container_entity()` creates an entity inside it |
| Static configuration | `config_module::find_static_entity(config_id)` returns `shared_ptr<static_entity>`; check for null, then read fields with their known schema types |

`table*` and `row*` are borrowed pointers, with no independent ownership of entity data. Do not retain them after removing the row, clearing the table or destroying the owning entity.

### Custom unsigned values

`entity::add_custom_u_int32(name, value)` creates a named runtime `uint32_t` value separate from schema fields. After creation, `set_custom_u_int32(name, value)` updates that value across the full range from `0` to `UINT32_MAX`; read it with `get_custom_u_int32(name)`. Setting a missing name or a name containing another scalar type returns `false` without creating data, converting its type or changing the existing value, in both Debug and Release builds. Use `find_custom_data()` to check existence; a stored zero is a valid value.

`check-kernel-initialization` exercises these operations through the public `entity` interface, including other custom scalar controls. With applications, Kernel/Utility and examples enabled, `check-typed-examples` also verifies unsigned updates on entities created by the real Kernel/EntityOperations libraries.

## Register events

Class callbacks cover creation / destruction, field changes, table changes and container operations. Individual entities can also have application event handlers. Register common class callbacks with `add_common_class_event()`.

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

Registration retains the object pointer. Keep `entity_observer` alive while its callback can run; do not register a local object that immediately leaves scope. The current interface has no matching public unregister method.

The ordinary `create_entity()` overload dispatches class events synchronously in this order:

| Stage | `entity_event` |
| --- | --- |
| Before loading | `entity_evt_pre_load_data` |
| Loading | `entity_evt_load_data` |
| Before applying data | `entity_evt_pre_effect_data` |
| Applying data | `entity_evt_effect_data` |
| After applying data | `entity_evt_post_effect_data` |
| Data stages complete | `entity_evt_data_finished` |

The application may explicitly dispatch `entity_evt_all_finished`; the creation function above does not send it automatically. Normal entity destruction dispatches `entity_evt_pre_destroy` followed by `entity_evt_destroy`.

Use `add_node_call_back()` and `add_table_call_back()` for field and table changes. Their arguments include the entity ID, changed location and old / new values. `do_event(id, event_id, args)` dispatches an application event for one entity. Consult the public headers for callback signatures and priority parameters.

## Maps and data conversion

Each map is identified by `map_id` and can have multiple instances. `create_map_instance()` returns an instance ID on success or `-1` on failure. `switch_map()` moves an existing entity to a target instance; the target map and instance must already exist.

The map module exposes entity lists and online counts. It does not provide a complete area-of-interest (AOI) system. Applications supply spatial indices, grid visibility and broadcast rules. `get_max_online_count()` currently returns a fixed value; it is not a deployment capacity limit or a capacity test result.

`entity_to_db_data()` converts an entity to `ark::protobuf::protobuf_db_entity`; the corresponding `create_entity(protobuf_data)` overload restores it. `entity_node_to_protobuf_data()` and `entity_table_to_protobuf_data()` can select fields by data mask. Conversion does not persist data to a database: applications own persistence, version compatibility and validation after restoration.

## API sources

- [Kernel interface](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/kernel/kernel_module.hpp)
- [Entity interface](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/kernel/entity.hpp)
- [Metadata interface](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/kernel/class_meta_module.hpp)
- [Configuration interface](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/kernel/config_module.hpp)
- [Map interface](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/kernel/map_module.hpp)
- [Data definitions and generation](../guides/data-structures.md)

## Kernel source layout

Kernel keeps all 38 existing headers beside its cpp files in `src/plugins/kernel/`.
The explicit `ark::kernel` mapping preserves every `<ark/kernel/...>` include,
including `data_impl.hpp`, `data_list_impl.hpp` and `kernel_plugin.hpp`, even when
the runtime plugin is disabled. Value/template definitions remain available to
business code. The four module implementations now inherit `module_base<their_interface>`;
`kernel_plugin` inherits `plugin_base<kernel_plugin>`. Core source headers live under `src/core/`.

The independent `tests/consumer/kernel` project links only `ark::kernel`, compiles
the 38 headers separately with Protobuf enabled, and runs value/template usage.
The unchanged `container_meta.hpp` requires `ark/define.hpp` and `ark/enum.hpp`
to be included first; the consumer supplies those existing prerequisites.
With `ARK_BUILD_PROTO=OFF` it checks the other 35 headers; `kernel_module.hpp`,
`kernel_module_impl.hpp` and `map_module_impl.hpp` retain their existing Protobuf
requirement. Use `check-core`, `check-kernel-initialization` and
`check-typed-examples` for data/node, initialization/configuration and application
regressions respectively. `check-kernel-initialization` requires
`ARK_BUILD_PROTO=ON`; `check-typed-examples` also requires the application, config
and example builds with Kernel/Utility runtime targets enabled.

```sh
cmake -S tests/consumer/kernel -B kernel-headers-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_FEATURES=application -DASIO_STANDALONE=OFF
cmake --build kernel-headers-build --target check-kernel-headers --parallel 2
```

## Typed Kernel construction

**Breaking ABI/layout change:** `class_meta_module_impl`, `config_module_impl`, `kernel_module_impl` and `map_module_impl` now inherit `module_base<their_existing_interface>`, while `kernel_plugin` inherits `plugin_base<kernel_plugin>`. All existing public types, constructors, methods and 38 header paths remain available. Rebuild Kernel and consumers together; old implementation/plugin binaries are not compatible with the changed class layout. No new compiler-independent plugin ABI is introduced.

The plugin uses private owning factories and explicit `DllEntryPlugin`/`DllExitPlugin`. `kernel_factory.hpp` stays inside the plugin source directory and is neither mapped nor installed. Factory return types preserve the original interface RTTI keys:

| Registration interface | Retained implementation | Update registration |
| --- | --- | --- |
| `class_meta_module` | `class_meta_module_impl` | `module_update::disabled`; inherited default update |
| `config_module` | `config_module_impl` | `module_update::disabled`; inherited default update |
| `kernel_module` | `kernel_module_impl` | `module_update::enabled`; existing override |
| `map_module` | `map_module_impl` | `module_update::disabled`; inherited default update |

Installation retains the table order; uninstallation uses its reverse. Registration still completes before lifecycle initialization, whose container traversal does not guarantee dependency order. Entity defaults, configuration/schema loading phases, Protobuf messages and business data formats are unchanged.

`check-kernel-plugin` is a `check-core` dependency when Protobuf and the Kernel/Utility runtime targets are available. The independent runner links only the public `ark::kernel` target and loads both actual DSOs. It checks all four interface keys and manager ownership, metadata/static record loading, map/entity registration, public lifecycle callbacks, manager updates, normal plugin exit and repeated stop. It does not inspect the registry after unloading or certify arbitrary hot update. The public-header consumer also checks the typed bases and retained update overrides; existing initialization/entity-default regressions remain required.

**Unsigned 64-bit getter migration:** `static_entity::get_u_int64()` and both `static_entity_impl` overrides now return `uint64_t`, preserving values through `UINT64_MAX`. Update custom overrides and variables that incorrectly used `int64_t`, and rebuild Kernel together with its C++ consumers. Signed fields continue to use `get_int64()`.
