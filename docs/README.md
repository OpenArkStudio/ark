# ark

ark is a C++20 framework for distributed applications. Applications assemble plugins; plugins register modules that provide business and infrastructure interfaces.

Development uses CMake, permanent C++ tests and cross-platform CI. The independent [arknet library](https://openarkstudio.github.io/arknet/) provides TCP, WebSocket, HTTP and RDP.

<Aside type="note" title="Choose the right layer">
Use ark to compose application services. Use arknet directly when you need a networking library. IO execution belongs to arknet; ark dispatches business callbacks through its `update()` loop.
</Aside>

## Where to go next

<CardGrid data-home-links>

<LinkCard title="Install and build" href="get-started/installation.md" description="Build C++20 applications and tests." />
<LinkCard title="Understand ARK" href="concepts/architecture.md" description="Applications, plugins, modules and lifecycle." />
<LinkCard title="Create a plugin" href="guides/plugins.md" description="Register modules and run the provided examples." />
<LinkCard title="Networking and tests" href="networking.md" description="Use arknet and run regression tests." />

</CardGrid>
