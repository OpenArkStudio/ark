<p align="right">English · <a href="README_CN.md">简体中文</a></p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/OpenArkStudio/.github/main/assets/ark-logo-dark.svg?v=20261022">
    <img src="https://raw.githubusercontent.com/OpenArkStudio/.github/main/assets/ark-logo-light.svg?v=20261022" alt="ARK" width="337">
  </picture>
</p>

<h3 align="center">A C++20 framework for modular server applications.</h3>

<p align="center">Compose services. Model entities. Connect processes.</p>

<p align="center">
  <a href="https://github.com/OpenArkStudio/ark/actions/workflows/ci.yml"><img src="https://github.com/OpenArkStudio/ark/actions/workflows/ci.yml/badge.svg?event=merge_group" alt="Merge queue CI"></a>
  <a href="CMakeLists.txt"><img src="https://img.shields.io/badge/C%2B%2B-20-5267df" alt="C++20"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache--2.0-5267df" alt="Apache-2.0"></a>
</p>

<p align="center"><a href="docs/get-started/installation.md"><strong>Get started →</strong></a> &nbsp; · &nbsp; <a href="https://openarkstudio.github.io/ark/">Documentation</a> &nbsp; · &nbsp; <a href="examples/">Examples</a> &nbsp; · &nbsp; <a href="docs/project/roadmap.md">Roadmap</a></p>

---

## Features

- **Services:** plugins and modules with lifecycle hooks; business plugins stay in their application repositories.
- **Entity data:** properties, tables and containers with metadata, configuration defaults and change callbacks.
- **Incremental state:** selected scalar and nested table/container-tree baselines/deltas; the [Godot demo](https://github.com/OpenArkStudio/ark-client-godot) uses Protobuf for movement and HP/MP synchronization.
- **Communication:** TCP, WebSocket, HTTP and RUDP through [arknet](https://github.com/OpenArkStudio/arknet), with bus identifiers and message routing.
- **Tooling:** Protobuf messages and a native [CSV/XLSX-to-Entity JSON exporter](docs/guides/tools.md) with C++/C# field indices; the Python/XML importer remains available for legacy resources.

## Get started

Build on Linux, macOS or Windows with **C++20**, **CMake 3.21+**, Ninja and vcpkg. Windows requires **Visual Studio 2022+**.

Configure `app-build/` using the [installation guide](docs/get-started/installation.md), then build:

```sh
cmake --build app-build --config Release --parallel 2
```

Follow the [quick start](docs/get-started/quick-start.md) to prepare the runtime binaries and configuration. On macOS/Linux, start Timers from `run/timers/`:

```sh
./bin/app --busid=1.0.1.1 --name=timers \
  --app_conf=conf/timers.app.toml
```

Logging shares the application manifest; omitted settings use console INFO output. See [configuration and migration](docs/service-manifests.md#embedded-logging).

The [Godot C# demo](https://github.com/OpenArkStudio/ark-client-godot) pairs with [`guest_room`](examples/guest_room/) to demonstrate shared movement, HP/MP attributes and reconnecting. The [legacy Unity client](https://github.com/OpenArkStudio/ArkClient-Unity3D) has not been verified against the current ark server.

[Module guide](docs/guides/modules.md) · [Architecture](docs/concepts/architecture.md) · [Tests](docs/networking.md) · [Contribute](.github/CONTRIBUTING.md) · [Discord](https://discord.gg/GmyBbcv)

Core developers: [Nick Yang](https://github.com/NickYang1988), [flyicegood](https://github.com/flyicegood) and [loscaz](https://github.com/loscaz). Thanks to all [contributors](https://github.com/OpenArkStudio/ark/graphs/contributors).

---

[Apache-2.0](LICENSE) · Copyright [OpenArkStudio](https://github.com/OpenArkStudio).
