# ark

ark 是用于构建分布式应用的 C++20 框架。应用组合插件，插件注册模块，模块提供业务与基础设施接口。

CMake 构建、正式 C++ 测试与跨平台 CI 支持开发。独立的 [arknet 网络库](https://openarkstudio.github.io/arknet/zh/)提供 TCP、WebSocket、HTTP 和 RDP。

<Aside type="note" title="选择合适的层级">
使用 ark 组织应用服务；需要网络库时可直接使用 arknet。IO 执行由 arknet 负责，ark 通过 `update()` 循环分发业务回调。
</Aside>

<h2 id="where-to-go-next">接下来</h2>

<CardGrid data-home-links>

<LinkCard title="安装与构建" href="get-started/installation_CN.md" description="构建 C++20 应用与测试。" />
<LinkCard title="了解 ARK" href="concepts/architecture_CN.md" description="应用、插件、模块与生命周期。" />
<LinkCard title="创建插件" href="guides/plugins_CN.md" description="注册模块，运行仓库示例。" />
<LinkCard title="网络与测试" href="networking_CN.md" description="使用 arknet，运行网络回归测试。" />

</CardGrid>
