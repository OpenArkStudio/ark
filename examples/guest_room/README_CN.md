# 访客房间

[English](README.md)

通过真实 ark 启动器、Utility、Kernel、Bus 与 Net 插件运行的 2D 联机示例。
两个 [Godot C# 客户端](https://github.com/OpenArkStudio/ark-client-godot)
通过拥有型 Protobuf 基线与增量，观察彼此的移动、HP/MP、背包和装备。
不需要数据库或服务发现服务。

| 交互 | Entity 数据 |
| --- | --- |
| 移动、受击、技能、恢复 | 带类型的标量字段 |
| 拾取／消耗药水 | 背包表格行、数量及行存续期 |
| 换装／强化 | 装备容器成员及子 Entity 字段 |
| 离开／重连 | 根删除标记、新身份与完整基线 |

业务修改和捕获均在启动器所有者线程执行。客户端提交意图，展示服务端确认的状态。
JSON 定义字段和初始业务配置，可复用的嵌套发布端／接收端拥有待处理值。
房间声明 Kernel、Map 与 Net 依赖，逆序清理时先释放玩家状态和回调，再停止这些依赖模块。

## 构建与运行

```sh
cmake -S . -B out/guest-room -DCMAKE_BUILD_TYPE=Release -DARK_BUILD_GUEST_ROOM_EXAMPLE=ON
cmake --build out/guest-room --target app guest_room_plugin ark_kernel_plugin ark_utility_plugin ark_bus_plugin ark_net_plugin --parallel 2
bash examples/guest_room/run.sh --bin-dir out/guest-room/bin --runtime-dir /tmp/ark-guest-room
```

先满足[应用构建前提](../../docs/get-started/installation_CN.md)，并启用 Utility、Kernel、Bus 与 Net
运行时插件。[run.sh](run.sh)说明启动参数，[Godot 指南](https://github.com/OpenArkStudio/ark-client-godot#readme)
说明客户端准备方式。已有 Login／CS／SS 监听器不实现此示例协议。

## 验证

```sh
cmake --build out/guest-room --target check-guest-room
```

编解码目标检查 C++／C# 共享向量、类型值、非法输入、消息帧及 16 人基线体积。
POSIX 服务端目标启动真实启动器和两个原生客户端，覆盖移动、行删除／复用、
装备替换／补丁、错误、移除、重连及正常退出。它使用独立运行目录和端口 19191，
请勿同时启动多份。真实 Godot 执行属于单独的客户端集成检查。

[房间协议与上限](protocol_CN.md) · [通用 Entity 协议](../../docs/reference/entity-protocol_CN.md)
· [停机所有权](../../docs/project/lifecycle-closeout_CN.md)

**不兼容变更：** 当前通用 Entity 消息替换旧的纯标量房间协议，服务端与 Godot 客户端须一起
重新构建，不保留兼容分支。本机功能检查不构成生产容量验收。
