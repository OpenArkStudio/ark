# Entity operations example

This plugin loads through `config/entity_operations.app.toml` with Kernel and Utility. Its public `entity_operations_module` type stays in the adjacent header; a private factory, `module_base<module>`, `plugin_base` and explicit DLL entries handle construction. It retains the module RTTI key and does not participate in update.

Startup creates map 1 and four Player entities, sets the name to `Xiaoming`, initializes gender/career/camp/last-map/level to 1, creates an item row with GUID 1111 and custom integer `test=111`, demonstrates name/index data access and final gender 11 for one player, and adds an item row with count 22. Existing callback demonstration methods remain; startup registers its bag callback. Shutdown destroys the entities. There are no timed or large loops in startup.

Stage a complete matching build and the supplied example metadata before running:

```sh
mkdir -p run/entity_operations/bin run/entity_operations/conf
cp -R app-build/bin/. run/entity_operations/bin/
cp config/entity_operations.app.toml config/kernel.plugin.conf run/entity_operations/conf/
cp -R examples/entity_operations/resource run/entity_operations/
cd run/entity_operations
./bin/app --busid=1.0.1.1 --name=entity_operations --app_conf=conf/entity_operations.app.toml
```

Use `app_d` for Debug; stop with Ctrl+C. The metadata is a small runnable example, not exported production business data. `check-typed-examples` loads the real DSO and verifies its four entities, fields, item rows, custom values, compile-time module/update contract and lifecycle; `check-core` includes it. The old timed workload is now an explicitly launched [C++ benchmark](../../benchmarks/ENTITY_OPERATIONS.md), independent of startup.
