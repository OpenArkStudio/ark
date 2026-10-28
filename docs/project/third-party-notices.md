# Third-party notices

ark uses [Apache-2.0](https://github.com/OpenArkStudio/ark/blob/main/LICENSE). Runtime package licenses are listed under [dependency licenses](../reference/third-party-notices.md).

| Dependency | Purpose | License |
| --- | --- | --- |
| arknet | Networking | [BSL-1.0](https://github.com/OpenArkStudio/arknet/blob/main/LICENSE) · [dependencies](https://openarkstudio.github.io/arknet/project/third-party-notices/) |
| OpenXLSX 0.5.1 | XLSX import | [BSD-3-Clause](https://github.com/troldal/OpenXLSX/blob/v0.5.1/LICENSE.md) |
| miniz 3.0.2 / 3.1.2 | XLSX ZIP dependency | [MIT](https://github.com/richgel999/miniz/blob/3.1.2/LICENSE) |
| pugixml 1.15 / 1.16 | XLSX XML dependency | [MIT](https://github.com/zeux/pugixml/blob/v1.16/LICENSE.md) |
| nlohmann/json 3.12.0 | Entity JSON import/export | [MIT](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT) |
| Boost.Nowide | Windows UTF-8 paths | [BSL-1.0](https://www.boost.org/LICENSE_1_0.txt) |
| Cloudflare Nimbus | Documentation | [MIT](https://github.com/cloudflare/nimbus/blob/main/LICENSE) |
| Inter / JetBrains Mono | Documentation and social-card fonts | [Inter OFL](/fonts/OFL-Inter.txt) · [JetBrains Mono OFL](/fonts/OFL-JetBrains-Mono.txt) |

The exporter dependencies are optional; loading its JSON output does not require OpenXLSX. Direct-source and pinned vcpkg builds select different miniz/pugixml versions. Original texts remain in [docs/licenses/config-tool](https://github.com/OpenArkStudio/ark/tree/main/docs/licenses/config-tool); use the notices supplied with the actual packages. Other documentation-build dependencies retain their licenses in the locked npm packages.
