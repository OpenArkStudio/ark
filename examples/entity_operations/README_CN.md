# 实体操作示例

此插件通过 `config/entity_operations.app.toml` 与 Kernel、Utility 一起加载。公开的 `entity_operations_module` 类型继续放在同目录头文件中；私有工厂、`module_base<module>`、`plugin_base` 和显式 DLL 入口负责创建，保留模块 RTTI 注册键，不参与 update。

启动创建地图 1 和四个 Player 实体，将名称设为 `Xiaoming`，初始化 gender/career/camp/last-map/level 为 1，创建 GUID 为 1111 的物品行和自定义整数 `test=111`。演示按名称／索引访问数据，其中一个玩家的 gender 最终为 11，并增加 count 为 22 的物品行。原有回调演示方法继续保留，启动会注册 bag 回调；停机销毁实体。启动不再执行计时或大循环。

运行前准备同次构建的完整产物及示例元数据：

```sh
mkdir -p run/entity_operations/bin run/entity_operations/conf
cp -R app-build/bin/. run/entity_operations/bin/
cp config/entity_operations.app.toml config/kernel.plugin.conf run/entity_operations/conf/
cp -R examples/entity_operations/resource run/entity_operations/
cd run/entity_operations
./bin/app --busid=1.0.1.1 --name=entity_operations --app_conf=conf/entity_operations.app.toml
```

Debug 使用 `app_d`，以 Ctrl+C 停止。附带元数据是可运行的小型示例，不是生产业务导出数据。
`check-typed-examples` 加载真实 DSO，验证四个实体、字段、物品行、自定义数据、编译期模块／update 契约和生命周期；`check-core` 包含此检查。
旧计时负载已移入显式启动的 [C++ 基准](../../benchmarks/ENTITY_OPERATIONS_CN.md)，与应用启动分离。
