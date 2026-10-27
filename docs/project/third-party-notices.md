# Third-party notices

ark is distributed under [Apache-2.0](https://github.com/OpenArkStudio/ark/blob/main/LICENSE). Dependencies retain their own licenses in their source or distribution packages. The independent arknet library uses BSL-1.0; consult its [third-party notices](https://openarkstudio.github.io/arknet/project/third-party-notices/).

The documentation uses [Cloudflare Nimbus](https://github.com/cloudflare/nimbus) 0.17.0 and its 0.7.11 starter. The MIT license is preserved in [docs/site/LICENSE](https://github.com/OpenArkStudio/ark/blob/main/docs/site/LICENSE). Astro, Tailwind, Pagefind and the locked npm packages retain their applicable licenses. These are documentation-build dependencies.

## Native Entity configuration exporter

The optional `ark_config_tool` uses the following dependencies. The copied texts
are kept in `docs/licenses/config-tool/`; redistribute applicable notices with
the executable. The framework runtime loads the generated JSON without OpenXLSX.

| Dependency | Purpose | License text |
| --- | --- | --- |
| OpenXLSX 0.5.1 | XLSX reading | [BSD-3-Clause](https://github.com/OpenArkStudio/ark/blob/main/docs/licenses/config-tool/OpenXLSX-LICENSE.md) |
| miniz 3.0.2 | OpenXLSX ZIP dependency | [MIT](https://github.com/OpenArkStudio/ark/blob/main/docs/licenses/config-tool/miniz-LICENSE.txt) |
| pugixml 1.15 | OpenXLSX XML dependency | [MIT](https://github.com/OpenArkStudio/ark/blob/main/docs/licenses/config-tool/pugixml-LICENSE.md) |
| nlohmann/json 3.12.0 | Shared Entity JSON parser/serializer | [MIT](https://github.com/OpenArkStudio/ark/blob/main/docs/licenses/config-tool/nlohmann-json-LICENSE.txt) |
| Boost.Nowide | UTF-8 paths/arguments on Windows | [Boost Software License 1.0](https://github.com/OpenArkStudio/ark/blob/main/docs/licenses/config-tool/Boost-LICENSE.txt) |

OpenXLSX and its dependency texts come from the 0.5.1 source dependency set;
nlohmann/json's text comes from its 3.12.0 source package. The Boost text is the
[official license](https://www.boost.org/LICENSE_1_0.txt). Dependency packages may
provide additional notices for their exact build; retain those when distributing
different versions or linked components. The legacy Python/openpyxl workflow has
its own package dependencies and does not become part of the native executable.

Inter and JetBrains Mono use the SIL Open Font License 1.1. The font notices are included in the site's [Inter license](/fonts/OFL-Inter.txt) and [JetBrains Mono license](/fonts/OFL-JetBrains-Mono.txt); this also covers the bundled Inter font used for social cards.
