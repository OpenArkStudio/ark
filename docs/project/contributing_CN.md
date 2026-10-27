# 贡献与文档开发

改动通过 PR 提交到受保护的 main 分支。完成对应检查后，以英文提交标题 squash 合并。详见[贡献指南](https://github.com/OpenArkStudio/ark/blob/main/.github/CONTRIBUTING.md)。

## 开发文档

文档站使用 Cloudflare Nimbus 与 Astro。`docs/` 中的 Markdown 是内容源，每页包含英文文件及对应的 `_CN.md` 中文文件。生成的内容和站点输出不提交到仓库。

```sh
npm ci --prefix docs/site
npm run dev --prefix docs/site -- --host 127.0.0.1
npm run check --prefix docs/site
```

站点提供语言切换、搜索、代码高亮，以及供 AI 阅读的纯 Markdown 接口。PR 只运行文档检查，不发布站点。变更合并到 `main` 后，文档工作流构建并发布 GitHub Pages；维护者也可手动触发。文档构建需要 Node.js 22.23.4+；编译 ark 与 C++ 测试不需要 Node.js。


## CI 文档范围

根目录的精确文件 `AGENTS.md` 归为文档。改动范围已知且非空、仅包含已识别的文档，
并且 Nimbus 文档站可用时，源分支 PR 与合并候选均要求文档检查和 `CI - required`，
原生任务明确跳过。Nimbus 检查文档站；只有根 README 变化时才检查其链接。

嵌套的未知 `AGENTS.md` 路径、名称相近的代码、混合源码变更、范围不可用或文档站不可用时，
仍保留原生检查。手动运行保留完整原生矩阵。CI 策略本身的变更仍须完成原生验收，
这项文档例外不会改变该要求。

Applications 任务的依赖准备、编译与回归共用 60 分钟总预算。Windows 未命中缓存时，
仅依赖配置就可能耗时约 26 分钟。各测试自身的限时和全部必需检查保持不变；
取消的任务不算通过，需要重新完成一个成功的合并候选检查。

## CI 依赖包归档缓存

CI 使用 `actions/cache` 恢复执行机临时目录中的 vcpkg 包归档。
带版本号的 v2 key 由兼容前缀和精确执行机镜像后缀组成。
前缀包含执行机操作系统与架构、`ImageOS`、固定的 vcpkg 版本、manifest 和配置文件内容
（含 baseline）、本地 port overlay、选中和默认 feature、构建 profile、编译器环境参数，
以及 target/host triplet 覆盖项。只有 `ImageVersion` 移到后缀。
在 Actions 搜索的每个引用内，先查找精确 key，再使用这一个前缀，不提供仅按操作系统或跨 profile 的回退。
两个镜像字段仍为必填项。

恢复归档只为 vcpkg 的 `files` provider 提供候选包，不直接恢复依赖安装目录。
vcpkg 为每个请求的包计算完整 ABI hash，其中保留编译器跟踪；只有 hash 匹配时才取用归档，
不匹配的包仍从源码构建。工作流不会关闭编译器跟踪、覆盖 ABI hash 或重命名包归档。
具体规则见 [vcpkg ABI 不匹配说明](https://learn.microsoft.com/en-us/vcpkg/users/binarycaching-troubleshooting#cause-3-differences-between-local-and-remote-build-environments)。
因此，滚动更新的镜像可以共享归档缓存，但恢复成功不代表其中每个包都兼容。

配置成功后，未命中或通过前缀恢复的运行只按精确 key 保存一次，不另外按兼容前缀保存第二份。
各镜像的快照仍可能包含重复归档并占用缓存空间。v1 key 无法通过这个前缀安全查找，因此不参与回退；
可访问的运行尚未保存相同依赖配置的 v2 缓存时，v2 仍需冷构建。
Actions 缓存的分支／引用可见性规则继续生效，详见 [Actions 缓存规则](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#cache-key-matching)。
本次改动不能让另一个合并队列引用的缓存变得可读。
相互无法读取缓存的队列运行需要默认分支中兼容的 v2 归档作为初始来源。

身份信息缺失、依赖布局或归档路径不受支持时，继续关闭持久缓存并导出 `VCPKG_BINARY_SOURCES=clear`。
缓存服务失败不影响正常的源码配置、编译和回归检查。依赖缓存仅覆盖包归档，不包含依赖安装目录。项目目标文件缓存单独配置，见下文。
本地 Node 测试验证查找边界、输出与工作流连接；托管环境中的缓存复用和耗时变化仍需独立 CI 证据。


### Windows ABI 诊断

依赖配置及原有的归档保存步骤之后，Windows Applications 为 Asio、rapidxml 和 Protobuf
记录有界的 JSON 诊断。脚本只读取
`ci-build/vcpkg_installed/x64-windows/share/<port>/vcpkg_abi_info.txt`，
不搜索目录，也不调用构建工具。符号链接、非普通文件、格式错误、triplet 不符或超过 64 KiB 的文件均被拒绝。
元数据缺失或被拒绝时明确报告 `unavailable`；可选字段缺失时同样报告 `unavailable`，不将缺失视为相等。

`packageAbi` 是 ABI 元数据原始字节的 SHA256。字段值仅以 SHA256 指纹输出，字段名固定为：
`triplet`、`triplet_abi`、`features`、`cmake`、`powershell`、`compiler_hash`、`portfile.cmake`、
`ports.cmake`、`vcpkg.json`、`post_build_checks`、`sbom_info`、`vcpkg-cmake`、
`vcpkg-cmake-config`、`abseil` 和 `utf8-range`。
未知字段名和所有原始值均不输出，不记录完整环境、绝对路径或凭据，也不上传元数据文件或 artifact。
独立的 `compiler_hash` 字段可能不存在；诊断不会根据编译器版本字符串推断其值，也不拆解不透明的 `triplet_abi` 值。

此前步骤失败后，诊断仍会执行，任务取消时除外。状态只表示元数据是否可用，不能让配置、编译或测试的失败变为成功。
诊断错误也不会成为新增的原生验收条件。缓存 key/provider、编译器跟踪和构建设置均保持不变。
对比不同运行的指纹可以缩小 ABI 差异范围；已输出字段相同不能证明未输出的输入相同。
诊断不判定缓存命中，也不构成性能测量。此前分析的两轮 Windows 日志中，74 个包的 ABI 均发生变化；
导致变化的具体输入仍未得到证实。


## CI 编译目标文件缓存

Linux 和 macOS 原生任务可选使用 `Mozilla-Actions/sccache-action@v0.0.11`，
固定 sccache 版本为 `v0.16.0`。准备步骤先验证可执行文件的精确版本，再导出 C 和 C++ 的
CMake 编译器 launcher。编译器仍由 CMake 选择；平台、依赖 profile、sanitizer 参数、构建目标、
并行度、回归检查和超时设置保持不变。Windows Debug 任务直接编译；保留现有 `/Zi` 共享 PDB
配置，不为获得缓存命中而修改参数。

编译归档使用独立命名空间，与 vcpkg 包归档分开。兼容前缀采用现有依赖 key 的完整身份信息，
包括精确镜像版本、manifest/overlay 指纹、编译器环境，以及模块、sanitizer 和依赖 profile。
源码 commit 只作为不可变快照的后缀。兼容前缀和 `SCCACHE_C_CUSTOM_CACHE_BUSTER` 不包含运行 ID
或源码 commit，因此相同身份的后续 commit 可以恢复目标文件。编译缓存不跨镜像或 profile 回退。
sccache 仍逐次验证编译器身份、编译参数及源码和头文件输入；恢复归档不代表目标文件命中，
也不会绕过编译输入校验。

每个任务在执行机临时目录中设置 `SCCACHE_CACHE_SIZE=512M`。完整矩阵有九个可用的 Linux/macOS
任务，每个源码快照对应九份这样的本地缓存预算；Windows 不增加编译缓存。
Actions 保存不可变快照，不同 commit 和引用的快照可能重复占用空间，因此这不是仓库总存储上限。
压缩、元数据、依赖包归档和缓存服务保留策略需要单独计算。Actions 的分支／引用可见性规则仍然适用；
key 相同不能让不同合并队列引用相互读取缓存。

安装、版本验证、缓存恢复或服务启动失败时，launcher 为空并正常编译。
缓存服务 I/O 失败通过 `SCCACHE_IGNORE_SERVER_IO_ERROR=1` 回退。
编译或测试失败仍使任务失败；工作流不会在编译器报错后移除 launcher 自动重试。
只有成功的任务保存新的编译快照。尽力生成的任务摘要报告依赖归档状态、编译归档精确命中状态，
以及 sccache 统计。前缀恢复与目标文件命中是两类不同证据。

运行 `node --test tests/ci/*.test.mjs` 验证缓存身份、回退行为和工作流契约。
永久保留的 CMake/Ninja fixture 使用含空格路径的 launcher 编译 C 和 C++，并验证启用和关闭
launcher 时，故意引入的编译错误均会传播。fixture 使用透明转发脚本，不使用 sccache，
不能证明托管环境命中。托管验收需要对比冷任务与随后身份相同的任务，记录源码版本、归档查找结果、
sccache 命中／未命中，以及准备、配置、构建和测试耗时，并保留全部必要回归。
这些证据齐备之前不宣称构建提速。Linux/macOS 原生 CI 仍需分别验收；Windows 继续直接编译。
