# Historical roadmap

This snapshot preserves the previous roadmap, including its original wording and checkbox states. It is a historical record, not a list of supported APIs, accepted capabilities or current priorities. English and Chinese snapshots retain their original differences. Use the [current roadmap](roadmap.md) for supported behavior and remaining acceptance.

---

## Current modernization

- [x] Complete the `include/ark/`, `src/` and `src/plugins/<name>/` layout and snake_case API migration, including Protobuf source naming.
- [x] Release native single-node MySQL and Redis clients in arknet 0.2.0.
- [ ] Replace ark database plugins' legacy drivers, with Asio coroutine entry points, native arknet owning results and module-thread completion.
- [ ] Integrate the remaining independent official plugins with optional builds and tests.
- [ ] Review Nimbus documentation locally while preserving existing articles and entry points.

Hot-update discussions and implementation are deferred. The original planning list below retains its historical checkbox state; it is not a current API or capability list. Use the current items above, source code and acceptance records to determine implementation status.

## High priority

- [ ] Put all schemas in one Excel workbook and keep other configuration tables separate, while remaining compatible with the existing format.
- [x] Unify code style.
- [ ] Connect to logserver before process startup and fail startup if the connection cannot be established, so logging is available from the beginning.
- [x] Upgrade spdlog.
- [ ] Add a dirty data layer that batches changed data after each client request, instead of sending every change separately in small packets with repeated protocol headers.
- [ ] Complete documentation for concepts, design principles, server architecture and usage examples.
- [x] Align all basic data structures with C++ containers.
- [ ] Add service registry support, considering etcd or consul.
- [ ] Use timers for periodic logs and heartbeats instead of update and HeartBeat.
- [ ] Update configuration packaging: edit in Excel and generate `csv` files for the project to read. Use contributor-specific suffixes such as Item.A.xlsx and Item.B.xlsx to reduce collaboration conflicts.
- [ ] ~~Control plugin loading order through configuration fields or configuration order.~~
- [x] Use the AFXml wrapper instead of accessing rapidxml directly.
- [x] Simplify interface class design.
- [ ] Generate repetitive code (_plugin and module classes_).

## Low priority

- [ ] Update protocol and client handling for Map and Layer (**the concepts have changed; client integration is pending**).
- [ ] Use a different approach to sorting and swapping entries in inventory tables.
- [ ] Reduce data synchronization traffic: remove repeated strings such as `NodeName` and `TableName`, send index-to-name mappings at login, then use indices for updates and names for persistence.
- [ ] Extend DataTable to support more complex structures, such as items containing gems with experience and enchantments, or weapons with random attributes. The existing fixed two-dimensional structure is difficult to extend.
- [ ] Add network library and performance benchmarks under benchmark/unittest.
- [ ] Add kcp.
- [ ] Use SWIG for the scripting module.
- [ ] Evaluate ECS: represent all objects as entities and use GUIDs to identify their group and position for fast lookup. Research references include Overwatch, ET and [ECS-unity](https://github.com/sschmid/Entitas-CSharp).
- [ ] Add coroutines, referring to libgo.
- [ ] Introduce tcmalloc.
- [ ] Add a monitoring web page using vue-admin.
- [ ] Add a Deployment web tool.
- [ ] Add message queue processing.
- [ ] Add operations tools (tcm & deploy).
- [ ] Use multiple threads for selected CPU-intensive work.

## Partially finished

- [x] Add spdlog sinks that organize files by directory and rotate by size or hour (*partially changed; further changes are needed for the tlog format, /data/binlog/1001/20180627/game1_hour.log*).
- [x] Add consistent_hash_map (**added, not yet enabled**).

## Finished

- [x] Implement the new architecture.
- [x] Configure connection relationships instead of setting them manually in code (bus_relation, server_settting). Detect IP addresses and generate ports dynamically instead of specifying them, and express network addresses as URLs such as tcp://0.0.0.0:9001 and wss://0.0.0.0:9001.
- [x] Place Debug and Release binaries together under bin, with separate Debug and Release settings so they can share one configuration set.
- [x] Change guid to an internal pointer type to reduce memory use for other non-guid types. ***Changed to int64_t; the memory concern no longer applies.***
- [x] Add a network plugin to separate network modules from application logic (NetClient and NetServer are still too closely coupled to application logic).
- [x] Add endpoint to the net plugin.
- [x] Merge the kernel and config plugins because kernel depends heavily on config.

## Bugs

- [ ] ClearRow shifts subsequent row indices. Either preserve the indices and clear only the row contents, or have the client update subsequent indices when it receives DeleteRow.
- [x] Fix ConfigPlugin data loading.
- [x] Fix the return values of AFMapEx First and Next.
