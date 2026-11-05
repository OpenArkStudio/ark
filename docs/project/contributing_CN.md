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

| 改动 | 源分支 PR | 合并队列／手动验收 |
| --- | --- | --- |
| 已识别的纯文档 | Nimbus 与相关 README 链接检查 | 同样的文档检查 |
| 已登记的应用输入 | 相关检查目标及 CMake 依赖 | 完整跨平台矩阵与应用构建 |
| 公开接口、构建、依赖、CI 或未知输入 | 完整应用检查；范围不确定时保留所有模块 | 完整跨平台矩阵 |

所有 PR 均要求 `CI - required`。范围分类缺失或失败时回退完整检查。按目标构建不减少队列／手动验收、归档集成的构造检查、运行时检测或测试超时约束。草稿预览不代表通过验收。

## CI 构建资源

根据实际 CPU 和内存选择编译并行度，最多四路。7–8 GiB 执行机保持两路编译、一路链接；16 GiB 使用四路编译、一路链接；至少 19 GiB 才允许两路链接。这是调度估算，不保证单个编译单元的内存峰值。

Ninja 分别限制编译与链接任务。`ARK_BUILD_JOBS_OVERRIDE`、`ARK_LINK_JOBS_OVERRIDE` 只能降低计算出的上限。资源信息缺失时采用保守值。

## CI 依赖包归档缓存

vcpkg 包归档与项目编译对象分开缓存。依赖 key 保留系统／架构、manifest 与 overlay、feature、profile 和编译器环境。跨镜像小版本恢复后，仍由 vcpkg 的真实 ABI hash 决定能否使用包，不关闭编译器跟踪。

现有 **Dependency cache seed** 工作流在受信任的 `main` 准备 Windows 应用依赖，只配置依赖，不编译 ark 或重复原生回归。种子必须通过精确缓存查找验证。必需 CI 中缓存始终可选，缓存故障时正常编译。

### Windows ABI 诊断

诊断输出配置阶段可用的 vcpkg ABI 元数据，包括配置失败的情况。不推断缺失 hash、不改变依赖选择，也不让失败构建变为成功。

## CI 编译目标文件缓存

| 配置 | 行为 |
| --- | --- |
| Linux／macOS | 固定 sccache 0.16.0；实际编译器仍由 CMake 选择 |
| Windows Debug | 直接编译，保留原有 `/Zi` 配置 |
| 兼容性 | 保留依赖／profile 族；sccache 校验实际编译器、参数、源码和头文件 |
| 快照 | 每个兼容族、信任范围与 ISO 周保留一份不可变快照 |
| 应用快照 | 只由受信任的 `main` 写入，PR 和队列恢复使用 |
| 其他源 PR profile | 可在自身 PR 引用中保留一份每周快照 |
| 队列快照 | 只恢复，不按候选重复上传 |

**Compiler cache seed** 工作流编译 `main` 上已接受的 Linux／macOS 应用输入，不重复回归矩阵。同时保存匹配的依赖包归档，供后续 Unix 配置复用。已被新 main 替代的推送可跳过这项缓存工作；活动验收继续保留。Linux 应用缓存为 1 GiB，macOS 为 512 MiB，可配置范围为 64–1024 MiB。这是单任务本地预算，不是仓库总额度；不增加付费缓存空间、不删除现有远端缓存。

精确命中每周快照时，可以使用此前已接受版本的对象而不重新生成快照。普通 CI 仍编译并验证实际版本；新增输入可能未命中，直到下一份快照。归档命中不等于目标文件命中。[GitHub 缓存访问规则](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching)仍生效。

## 验证 CI 改动

    node --test tests/ci/*.test.mjs

测试覆盖范围回退、工作流边界、编译错误传播，以及真实 CMake／Ninja 的依赖和资源控制。原生验收与托管环境验收单独完成。

测量复用时，在 `main` 手动运行 **Compiler cache seed**，设置 `verify_reuse=true`。工作流仅清空该任务的本地编译对象，记录受控冷构建，再清理项目输出，以相同源码和设置记录热构建；远端缓存不变。摘要标注源码、归档查找、是否执行构建、样本类型和目标文件统计。比较依赖准备、编译与回归耗时后，才能声明提速。
