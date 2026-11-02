# CI 策略测试

使用原生 Node 测试入口，不需要安装 npm 包；Windows 运行时路径导出的跨平台测试需要 CMake 3.21 或更新版本：

```sh
node --test tests/ci/*.test.mjs
```

测试使用临时的本地 Git 仓库，不需要 GitHub API token、网络服务、C++ 重编译或数据库。
覆盖包含重命名旧路径的完整文件集合、按事件选择模块、文档站可用性，以及任务结果的严格校验。

源分支 PR 复用现有配置、编译和回归步骤，在 Linux Boost 上执行相关模块。
Core/Application 测试选择 Applications；网络测试选择 Network；数据库测试选择 Database。
Network、HTTP、Bus 插件变更还检查 Applications；MySQL、Redis 和共享数据库支持代码变更
检查 Applications 与 Database。公共核心头文件、共享构建或依赖、CI 和未知路径变更
检查全部三个源分支模块；枚举缺失、无效或为空时回退到全部 12 个任务的完整矩阵。
原生代码检查同时要求 clang-format 18 通过。文档和 README 链接检查沿用现有路径规则；
源分支 CI 成功只表示模块验证通过。

Merge Queue 候选直接比较事件中的 `base_sha` 与 `head_sha`，包含前序排队 PR 带来的变更。
候选包含代码、构建、依赖、CI 或未知路径时，执行全部 12 个原生任务：Network 的 Linux、macOS、
Windows，以及 Linux AddressSanitizer（内存错误检测）与 UndefinedBehaviorSanitizer（未定义行为检测）组合、
ThreadSanitizer（线程数据竞争检测）；Applications 的 Linux、macOS、Windows；Database 的
Linux Boost、Linux Standalone、macOS Boost、Windows Boost。手动运行保留相同完整矩阵。
只有范围已知、非空、文档站可用的纯文档变更才跳过原生任务。文档站存在时，合并候选仍检查文档；
README 变更还需通过其链接检查。

文档、根 README 链接和 C++ 格式检查合并到一个 `validation` job，共用一次 checkout 和 Node 设置。
文档站与锁文件可用时，Nimbus 复用 npm 包缓存；必需的文档站不可用时，预检仍会失败。
只有命令不存在时才安装 clang-format 18。各项检查保留原有范围标志，并导出实际步骤的 `outcome`；
前一项检查失败后，后续被选中的检查仍会执行。范围分类保留独立 job，分类结束后，原生矩阵与轻量验证可同时启动。
工作流定义由 8 个 job 减为 6 个；全部 12 个原生变体和所有轻量检查均被选中时，执行机任务由 17 个减为 15 个。
实际等待时间取决于执行机可用性和检查耗时。

required gate 核对当前工作流的真实事件和声明的验证阶段。选中的原生任务与合并后的验证任务必须成功；
不需要的原生任务必须明确跳过。选中的验证步骤必须报告 `success`，不需要的步骤必须报告 `skipped`。
合并任务成功不能替代缺失或被跳过的必需步骤。失败、取消、未知或缺失的结果均不通过；源分支结果不能替代队列结果。
最终验收仍要求包含前序变更的合并候选通过检查。受保护的 `main` 只接收已通过必需检查的候选；
合入后的推送不再对同一代码树重复执行 CI。独立的文档工作流仍在 main 推送后发布文档。
README 徽章展示合并候选的工作流状态。

草稿 PR 保留真实范围和必需检查标志，按范围执行分类、文档、根 README 链接和格式检查。
只有事件元数据中的 `draft` 是布尔值 `true` 时，才阻止新原生任务启动，
并将汇总任务命名为 `CI - draft preview`，使用独立预览校验。
预览通过要求 scope 成功、所有选中轻量步骤的实际结果为 `success`、未选中步骤为 `skipped`，
且全部原生任务为 `skipped`。摘要明确说明本轮未执行原生验收、PR 尚未获准入场。
预览不会输出成功的 `CI - required` 检查；最终门禁仍拒绝延后的入场请求。

因此，首次草稿运行不会创建 `CI - required` 上下文；保护规则仍保留这一精确必需名称。
预期的 Expected/pending 界面状态须在部署前通过托管草稿运行确认。
缺失或非布尔值的草稿元数据会回退到保守完整范围，保留必需名称及最终门禁，并被拒绝。
轻量检查失败仍使预览失败。

`ready_for_review` 触发新一轮源分支验证，沿用现有模块选择；普通 ready PR 的创建、更新和重新打开仍执行原有检查。
`converted_to_draft` 在新一轮运行中实施延后；这两类事件均不取消已启动的运行。
代码合并候选与手动运行仍要求完整原生矩阵；CI 策略变更仍须先通过全部源分支模块，再验收完整队列候选。
工作流本身不将 PR 加入队列。root 代理独占入场调度，按创建时间优先放行最早尚未完成的 PR；
前序 PR 验收前，新工作保留在本地或以关闭自动合并的草稿发布。仅因调度取消的运行仍须重新完成验收，才能获准入场。
已验证的同一 SHA 转回草稿不会清除旧检查上下文。须保持关闭自动合并，确认新一轮 `ready_for_review` 运行成功后，
才能开启自动合并或加入队列；同一 SHA 的旧成功记录不能替代新一轮入场检查。
动态名称本身不能强制执行这一新鲜度要求或全局入场顺序，代码候选仍须完成完整队列验证。

每个既有的 PR/引用并发分组最多执行一轮，只保留最新进入该分组的一轮等待任务。
新运行等待旧轮结束，不再自动取消正在执行的旧轮。每个已运行的 job 保留原有超时限制，
等待时间不计入这些限制；Merge Queue 候选使用各自的引用分组。独立分组不保证按 PR 创建顺序执行。
required gate 仍拒绝失败、取消或缺失的结果。手动取消仍可能使 `always()` 汇总无法完成。
详见 [GitHub 并发规则](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#concurrency)。

根目录的精确路径 `AGENTS.md` 也归为文档。范围已知且非空、文档站可用时，
仅修改该文件和其他已识别文档的 PR 与合并候选均要求 Nimbus 检查和最终必需门禁，
并明确跳过原生任务。这不会扩展根 README 链接检查，也不会豁免嵌套的 `AGENTS.md`、
名称相近的代码、混合源码变更或不确定范围。手动运行仍保留完整原生矩阵。
本次 CI 策略变更自身仍须完成原生验收。

`src/` 下仅将两个精确的插件索引路径 `src/plugins/README.md` 与 `src/plugins/README_CN.md` 归为文档。
文档站可用时，只修改这两个索引的 PR 与纯文档合并候选会选择 Nimbus 检查。
这不会将根 README 链接检查扩展到插件索引；Nimbus 验证生成的文档站，不直接检查索引文件。
`src/` 下的其他 Markdown、邻近构建／脚本、混合源码和不确定范围仍沿用保守的原生检查策略。

另外仅将四个精确开发说明路径 `tests/core/README.md`、`tests/core/README_CN.md`、
`tests/database/README.md`、`tests/database/README_CN.md` 归为文档。
它们说明如何构建和运行测试，不是运行时 fixture。文档站可用时，单独修改这些说明仍须运行 Nimbus，
不要求同时修改 `docs/`。根 README 链接检查不会扩展到测试说明，也不会放行其他测试 Markdown 或 README。
fixture、源码、构建／依赖／CI 输入、邻接未知路径、混合变更和不可用范围仍保留原生检查。
本次 CI 策略变更自身也必须完成全部原生模块验收。

另外两个既有手册 `tests/database/cancellation.md` 与
`tests/database/cancellation_CN.md` 只说明手动启用的命令，不作为构建或运行时输入读取、执行。
仅将这两个精确路径归为文档；混合数据库 C++ 变更仍选择 Database 源分支验收与 Nimbus。
未知测试 Markdown、脚本、构建／依赖输入仍按保守策略检查。Git 重命名保留原路径与新路径，
删除代码仍须原生验证；包含代码的合并候选继续执行全部原生矩阵。
本次语义分类修复自身仍须通过全部源分支模块与完整原生合并候选验收，
不会改变正在运行的检查，也不会豁免可执行工具。

只有根 README 变更时才检查其本地文件链接，包含图片、HTML `srcset` 和引用式链接。
检查器覆盖这些 README 使用的表达形式，不是完整的 Markdown 解析器；不验证远程 URL 或标题锚点。
Nimbus 检查单独验证生成文档站内的链接与锚点，不包含根 README 的标题锚点。

Windows Applications CI 通过 `VCPKG_APPLOCAL_DEPS=OFF` 关闭 vcpkg 对每个目标执行的本地依赖复制。
固定版本的工具链默认会在共享库和可执行文件构建后复制依赖，其中多个目标使用同一个输出目录。
配置完成后，`windows-runtime.cmake` 从真实的 `CMakeCache.txt` 读取依赖安装根目录、目标 triplet 与构建配置。
脚本要求对应的依赖 `bin` 目录存在，并通过 `GITHUB_PATH` 只导出该目录，使后续并行构建、回归目标与应用子进程继承此路径。
Debug 使用 `debug/bin`，发布类配置使用 `bin`，不回退到其他配置。缓存参数无效、运行时目录缺失或输出通道不可用时，任务明确失败。
Network、Database 和普通开发构建保留现有行为。测试实际执行 CMake 缓存加载、目录选择、子进程 PATH 继承与错误传递。
这一调整移除了共享输出目录中的本地依赖复制写入，但不能据此确认已观察到的 Windows 文件锁由哪个进程持有。
真实 DLL 加载和完整应用回归仍须由 Windows CI 验收。

vcpkg 缓存测试覆盖 Windows/Unix 归档路径、依赖和执行机镜像变化引起的 key 变化、
缓存配置导出、目录不可用，以及明确的无缓存构建路径。C++ CI 通过 Actions 缓存本地依赖包归档，
每个包的 ABI 仍由 vcpkg 校验。未命中缓存或缓存服务不可用时，依赖构建和测试照常执行；
归档在 CMake 配置成功后、ark 编译和测试之前保存；后续编译或测试失败仍会使任务失败。
工作流检查覆盖该顺序，并确保配置、编译和测试失败时不会忽略错误。缺少执行机镜像信息或归档路径不受支持时，
关闭持久缓存并导出 `VCPKG_BINARY_SOURCES=clear`，避免 run-vcpkg 再选择已移除的 `x-gha` 默认值。
缺少 GitHub 文件命令通道时明确失败。不缓存项目的 C++ 编译产物、依赖安装目录、凭据或编译器缓存。


v2 归档测试验证：镜像滚动更新后，精确 key 不同，但兼容前缀相同。
操作系统／架构、`ImageOS`、vcpkg 版本、baseline、依赖和 overlay 内容、feature 与默认项选择、
profile、编译器参数和 triplet 变化均隔离这两种查找。
工作流测试要求每个原生任务恰好恢复一次、按精确 key 保存一次，只向恢复步骤传递该前缀，
并保留 vcpkg 编译器跟踪及配置、编译和回归步骤的失败约束。
ABI 边界、v1 冷启动迁移和分支／引用可见性限制见
[CI 依赖包归档缓存](../../docs/project/contributing_CN.md#ci-依赖包归档缓存)。
这些测试不执行 vcpkg ABI 匹配，也不验证托管环境的缓存命中。


Windows ABI 诊断测试使用合成记录，格式已对照本地安装的 Asio、rapidxml 和 Protobuf 元数据确认。
测试覆盖原始字节的包 ABI 散列、固定字段值指纹、CRLF、格式错误／重复记录、元数据缺失、
固定安装路径、符号链接与 64 KiB 限制。CLI 测试确保输出不包含原始值、未知字段名、绝对路径或继承环境中的敏感值。
工作流测试保留仅在 Windows Applications 执行的范围，以及配置、编译和测试失败时的原有约束。
这些测试不执行 vcpkg 构建或托管缓存验收，详见
[Windows ABI 诊断](../../docs/project/contributing_CN.md#windows-abi-诊断)。
