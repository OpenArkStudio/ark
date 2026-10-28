# Protobuf messages and compatibility

Protocol definitions live in `src/proto/proto/`. Source files, messages, fields and enum identifiers use `snake_case`. The former `AFMsg` package is now `ark.protobuf`, exposed in C++ as `ark::protobuf`. The `consul` and `osslog` packages retain their names.

## Build and include

Enable `ARK_BUILD_PROTO=ON` and consume `ark::proto` to obtain generated include paths, `ARK_HAS_PROTOBUF` and the Protobuf runtime dependency. Application builds require this option.

```cmake
add_subdirectory(path/to/ark ark-build)
add_executable(my_service main.cpp)
target_link_libraries(my_service PRIVATE ark::core ark::proto)
```

Generated C++ files remain in the CMake build directory under `src/proto/generated/ark/proto/`. Edit the source `.proto` files and let CMake regenerate them; do not edit generated headers. Include a particular message family, for example `<ark/proto/common_messages.pb.h>`, or use the aggregate `<ark/proto.hpp>`. The `.pb.h` suffix belongs to the Protobuf generator and is not a project naming exception to rename manually.

Use Protobuf, Abseil and utf8_range from one compatible installation prefix. In a manual package selection, align `Protobuf_DIR`, `absl_DIR` and `utf8_range_DIR`; a successful link alone does not validate reflection/JSON behavior.

On Windows, `AFProto` / `ark::proto` builds one shared generated-message library. The host and loaded plugins import the same message classes and descriptors, including when the host creates a message for a public Kernel conversion call. CMake generates `ARK_PROTO_EXPORT` declarations with protoc and propagates import definitions through `ark::proto`; do not compile a second copy of the generated `.pb.cc` files into consumers or define the export macro manually. Other platforms retain the existing static AFProto library and unchanged generation command.

**Windows binary migration:** rebuild hosts and plugins together and deploy `AFProto.dll` (`AFProto_d.dll` for Debug) from the existing build `bin/` directory. The Windows host, plugins and message DLL must use the same compatible Protobuf runtime and build configuration. The DLL is installed to `bin/` by the existing RUNTIME rule; its import library goes to the library directory. Do not mix the previous static AFProto archive with the new import library, despite their retained `.lib` filenames. This changes Windows linking/deployment, not protocol field numbers or wire encoding.

`check-proto-configuration` configures both CMake branches and runs the real protoc to check message/descriptor export declarations, consumer imports and unchanged native generation. It does not compile or load Windows binaries on another platform. `check-kernel-plugin` retains its host-created Protobuf object and real cross-DSO conversion; Windows loader acceptance requires that platform's native run.

## Message serialization

Messages use the generated Protobuf API directly. The Bus module's message send paths in `src/plugins/bus/msg_module_impl.cpp` call `SerializeToString` and return `false` if serialization fails. The public `msg_module::decode_protobuf` helper in `<ark/bus/msg_module.hpp>` calls `ParseFromString` on the network message body, returning `false` on a parse failure and copying the actor ID on success. See [Bus](bus.md) for the message and transport boundary.

ark does not provide a pluggable message codec API. The generated messages and these Bus helpers are the existing serialization path.

## Migration contract

| Preserved | Requires consumer updates |
| --- | --- |
| Field numbers, types and cardinality | C++ type names and field accessors |
| Enum numeric values and binary payload encoding | Descriptor full names and `Any` type URLs |
| Explicit external JSON field names | Renamed JSON enum strings and code referring to textual type names |

Examples of current names are `ark::protobuf::protobuf_data`, `protobuf_node_data` and `protobuf_entity_data`. API helpers use `protobuf` rather than an abbreviated `pb` prefix; the bus module exposes `msg_module::decode_protobuf`. Rebuild dependent C++ code together with generated messages.

## ID fields and guest action enum migration

Regenerate Protobuf sources and update consumers to these field names. Generated C++ getters, `set_`, `mutable_` and related accessors change with the field name; packages, message names, field numbers, types and cardinality stay the same. Existing binary messages still parse with their original tags. Field-name reflection and Protobuf text-format field keys adopt the new names.

| Message | Previous field | Current field | Preserved JSON key | Tag |
| --- | --- | --- | --- | --- |
| `req_switch_server`, `ack_switch_server` | `selfid` | `self_id` | `selfid` | 1 |
| Same messages | `self_serverid` | `self_server_id` | `selfServerid` | 2 |
| Same messages | `target_serverid` | `target_server_id` | `targetServerid` | 3 |
| Same messages | `gate_serverid` | `gate_server_id` | `gateServerid` | 4 |
| `mysql_parameters` | `nreqid` | `request_id` | `nreqid` | 6 |
| `osslog.login` | `logid` | `log_id` | `logid` | 1 |

These existing JSON keys now have explicit `json_name` mappings, including the formerly implicit camelcase keys. Existing `SceneID`, `groupID`, Consul external keys and other `json_name` values stay unchanged. This change separates confirmed ID word boundaries without changing existing abbreviations, business vocabulary or spelling.

The four guest-room enum symbols change from `ACTION_UNSPECIFIED`, `ACTION_DAMAGE`, `ACTION_SKILL`, `ACTION_REST` to `action_unspecified`, `action_damage`, `action_skill`, `action_rest`, retaining numeric values 0, 1, 2, 3. Update C++ constant references and regenerate C# from the same schema. Protobuf binary frames and shared wire vectors stay unchanged. Tools using Protobuf JSON/text must adopt the new enum strings; these textual names are incompatible with the former uppercase spelling.

`check-proto-naming` uses the real protoc to build a descriptor set for `src/proto/proto/` and `resources/photon/proto/`, then checks lower_snake_case file names and hand-written messages, fields, oneofs, enums and enum values. It excludes protoc-generated map entry class names and proto3 optional synthetic oneofs, and verifies renamed field tags/types/JSON keys plus representative old binary and JSON messages. The target runs with `check-core`, or independently:

```sh
cmake --build app-build --target check-proto-naming --parallel 1
```

## External JSON spelling

Consul's C++ fields are lower case, while `json_name` explicitly retains its external keys. For example, `consul::service_data::meta()` corresponds to the JSON key `Meta`; `ID`, `Name`, `Address`, `Port`, `Tags` and `Check` retain their required spelling too. Do not replace these external keys just to match C++ identifiers.

The schema contract and representative wire/JSON regressions live in [consul.proto](https://github.com/OpenArkStudio/ark/blob/main/src/proto/proto/consul.proto) and [protobuf_messages.cpp](https://github.com/OpenArkStudio/ark/blob/main/tests/core/protobuf_messages.cpp). Changing textual descriptors is separate from binary compatibility; this migration does not promise complete source or JSON-enum compatibility.
