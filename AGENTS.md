# DiskANN cpp_main 论文复现工程指南

本文件适用于整个仓库。本项目用于围绕固定的 Microsoft DiskANN `cpp_main` C++ Baseline，建立可插拔、可追溯、可验证的 Partition / Build Local Graph / Merge Graph 论文复现与实验框架。

本文件中的“必须”、“不得”和“禁止”都是不可绕过的工程与实验闸门。`docs/source/framework.raw.txt` 是总体架构与语义边界的设计依据；对 Baseline 具体行为的判断，必须回到下述固定 commit 的源码及与该 commit 绑定的 `source-mapping.md`。

## 仓库边界与目录职责

- `upstream/diskann-cpp-main/`：只读、固定的上游 Baseline 子模块。
- `docs/source/`：原始设计和流程资料；除非任务明确要求修订原文，不得把实现记录或实验结果回写到原始资料。
- `.specify/` 与 `.agents/`：Spec Kit 模板、工作流与 Agent skill；不是论文算法实现目录。
- `specs/`：按需创建的公共契约、固定 Baseline 和单篇论文 Feature 规范；实验组合不得冒充新的算法规范。
- `contracts/`：ID、Artifact、Manifest、capability 和 Evaluation 契约及其版本。
- `src/`：公共流水线、论文实现、Baseline wrapper、Adapter 和 Registry。
- `harness/`：构建、执行、契约校验、评估和对比。
- `loop_engine/`：参数矩阵、依赖、复用、检查点和受控重试。
- `tests/`：本项目的单元、契约、小图性质和集成测试。
- `experiments/configs/`、`experiments/manifests/`、`experiments/artifacts/`、`experiments/reports/` 与 `experiments/results/`：分别保存实验声明、生成过程、不可变产物、评估报告和聚合结果；不得混用。

目录尚未存在或当前为空时，仅在本次任务需要时创建内容；不得为了“补齐架构”生成与任务无关的占位文件。

### Baseline Feature 路径治理

- 新的 Upstream Baseline Feature 以 short name `cpp-main-upstream-baseline` 作为语义身份；在 Spec Kit 创建它之前，不得在文档、脚本或命令中预先写死数字前缀。
- 创建前必须按 short name 检查 `specs/`：没有匹配项时只允许 Spec Kit 创建一次；恰有一个匹配项时必须复用该 Feature；存在多个匹配项或语义等价的 Baseline Feature 时必须停止并人工消歧，不得再创建一个冲突 Feature。
- Feature 创建后，所有必须使用目录路径的命令和引用都必须采用 Spec Kit 返回并写入 `.specify/feature.json` 的实际目录。不得根据当前最大编号、历史架构示例或 Feature short name 自行拼接数字路径。
- 现有 `specs/001-modular-cpp-diskann-pipeline/` 的目录和编号保持不变；Baseline Feature 不得占用、迁移或重命名该目录。

## 固定 Baseline 与上游保护

当前固定 Baseline 身份为：

```yaml
repository: https://github.com/microsoft/DiskANN.git
branch: cpp_main
commit: 78256bbab4685e1774e78d331e081a153be26823
language: C++
source_path: upstream/diskann-cpp-main
```

1. `upstream/diskann-cpp-main/` 是只读 Baseline。禁止在其中编辑、添加、删除、格式化或生成文件；构建与测试必须使用 upstream 目录之外的 out-of-tree 路径。不得更改子模块 commit 指针，除非人工明确批准一次 Baseline 迁移；迁移后必须重做 source mapping 和 Baseline Parity Gate。
2. 论文实现、Baseline wrapper/Adapter、契约校验器和实验代码必须位于 `upstream/` 之外。禁止通过覆盖上游代码来复现论文或加入 instrumentation。
3. 任何有效 Baseline 或论文对照运行都必须在 BuildManifest 中绑定并记录：`repository`、`branch`、完整 40 位 `commit`、工作区修改状态、compiler 及版本、完整 build flags、dataset/checksum，以及能完整重放运行的所有实验参数。建议同时保存硬件、OS 和资源预算指纹。
4. 不得使用“当前 `cpp_main`”、“最新 `cpp_main`”或其他浮动引用作为 Baseline。上述必填身份或复现信息任一缺失时，该运行必须标记为 invalid，不得进入对比或结果聚合。
5. Upstream Faithful、Instrumented 和 Deterministic Experimental 必须使用不同 `implementation_id`/`reproduction_mode`。固定 seed 或任何其他语义改动不得伪装成 Upstream Faithful Baseline。

## `merge_shards` 的 Baseline 语义边界

对当前固定 commit，Baseline `merge_shards` 的描述必须同时与 `upstream/diskann-cpp-main/src/disk_utils.cpp` 中的实现及 short name 为 `cpp-main-upstream-baseline` 的 Feature 内 `source-mapping.md` 中绑定该 commit 的映射一致。该 Feature 创建后必须使用 Spec Kit 实际生成的目录定位；如 Feature 或 source mapping 尚未建立，必须先建立并审查，不得以推测或预设数字路径代替源码映射。

M0 只包含以下语义：

1. 读取每个 shard 的 LocalToGlobal ID map，建立 GlobalNodeId 到 shard 的反向关系。
2. 将 shard medoid 和局部邻居从 LocalNodeId 转换为 GlobalNodeId。
3. 聚合同一 GlobalNodeId 在其所有 shard 副本中的局部邻接候选。
4. 对候选去重，随机打乱，再截断到 `max_degree=R`。
5. 以 GlobalNodeId 空间写出合并图。

不得将 M0 描述成向量距离重算、距离剪枝、`RobustPrune`、block-aware pruning、global refinement、连通性检测或连通性修复。若实现这些增强能力，必须建立 Baseline 之外的独立 implementation，声明新能力、语义偏差和测试。

## ID 空间、Artifact 与 Adapter

1. 标准 `LocalGraphArtifact` 默认使用 `id_space: local`；其邻接表中的节点和邻居都是该 Partition 内的 LocalNodeId。
2. 每个局部图产物必须保存完整 `LocalToGlobalIdMap`，且其长度必须等于 `local_node_count`。映射值、局部邻居和最终全局邻居都必须通过范围、完整性与 checksum 校验。
3. 使用 global ID、论文专有 ID 或其他存储布局的实现，必须通过显式 Adapter 接入标准契约。Adapter 位于 `upstream/` 之外，具有独立版本/implementation ID、`requires`/`provides`、契约测试和偏差记录；不得为了适配而静默改变论文核心语义。
4. 最终 `GlobalGraphArtifact` 必须使用 GlobalNodeId，不得残留 LocalNodeId、未解析副本或越界邻居；逻辑节点数必须与数据集节点数相等。

## Artifact、BuildManifest 与 EvaluationReport 分层

三者必须作为独立对象、独立文件/引用和独立 schema 管理：

- Artifact 是不可变数据产物，只回答“数据是什么”；不得混入 Recall、QPS、可达率或运行时性能结论。
- BuildManifest 回答“如何生成 Artifact”，必须包含实现、源码、工具链、输入、完整参数、输出、时间、资源、状态和失败原因。
- EvaluationReport 保存结构合法性之后的图质量、Recall、吞吐、延迟、资源指标和对比结论；它不是 Artifact 或 BuildManifest 的字段集合。

任何代码、schema 或实验布局变更都必须维持这一分层。

## 实现能力声明与执行闸门

1. 每个 Partition、Local Build、Merge、后处理或 Adapter implementation 都必须在 Registry/兼容性声明中显式定义 `requires` 和 `provides`。声明至少覆盖 Artifact 类型与版本、输入/输出 ID 空间、必需/提供的 capabilities，以及所需 runtime services。
2. Harness 必须在运行前检查所有上游 `provides` 能否满足下游 `requires`，并检查格式版本、ID 空间和数据集身份。不兼容组合必须在启动昂贵任务之前被拒绝。
3. 每个新生成或复用的 Artifact 都必须先通过与其类型和版本对应的 contract validator。未校验、校验失败、checksum 不匹配或输出不完整时，状态必须是 `INVALID_OUTPUT` 或 `FAILED`，禁止进入任何下游执行、评估、对比、缓存复用或结果聚合。
4. Contract validator 只证明格式和结构合法；质量、性能与论文特定性质由 Evaluator 和 paper-specific checks 证明。不得用其中一类验证替代另一类。

## 实验安全、公平性与可复现性

1. 未经人工明确批准，不得启动数据规模超过 SIFT1M 的任何构建、搜索、评估、参数搜索或批量实验。配置存在不等于获得执行批准；Agent 可生成大数据实验计划，但在闸门前必须停止。
2. 任何会改变算法语义的代码变更，必须依次完成并通过：适用的单元测试 → 小图性质测试 → SIFT1M 验证 → 人工 diff 审查。只有在人工审查通过并对该次运行给予批准后，才允许运行 SIFT100M 或更大数据集。
3. 已冻结 Baseline 的所有参数都是其身份的一部分，不得静默修改。任何参数变体必须具有新的实验/implementation 身份，并在对比中显式标注。
4. 参数搜索必须在运行前作为显式实验声明，预先记录搜索空间、生成方式、资源预算、重复次数、停止条件和聚合/选择规则。必须保留全部试验配置与结果，包括失败和无效运行及其原因。
5. 多次运行必须按预先声明的统计协议聚合。不得仅选取最优、最快或最符合预期的单次结果，不得丢弃“不好看”的有效结果。任何排除都必须依据事先声明的规则并留下可审计记录。
6. 对比运行必须固定查询集、ground truth、距离度量、硬件/线程预算、缓存条件和计时范围，或明确记录差异及其原因。不得通过降低 Recall、跳过查询或改变计时口径来制造优势。
7. 任何论文复现结论都必须区分 Exact reproduction、Faithful adaptation 与 Inspired implementation，并分别记录论文声称、官方源码、当前适配和已知偏差。

## 开发与验证工作流

1. 修改前先检查 Git 状态、相关 spec/contract/source mapping、现有实现和仓库中真实存在的测试入口。保留用户已有修改，不得为了本任务重置或覆盖无关工作。
2. 变更公共契约时，同步更新 schema/version、validator、Registry capability、Adapter、测试和相关 spec。破坏性变更必须升级格式版本，不得让旧 Artifact 被误解析为新格式。
3. 添加或修改 implementation 时，同时维护实现身份、`requires`/`provides`、算法/源码映射、偏差记录、完整参数和适用测试。
4. 修改代码后，必须运行仓库中实际存在且适用于本次修改的测试。先从最小相关的单元/契约测试开始，再运行必要的集成、小图性质、Baseline parity 或 SIFT1M 验证。不得编造不存在的命令，也不得用 lint/compile 代替应运行的行为测试。
5. 上游自带测试可用于 Baseline wrapper/parity 验证，但必须从只读源树做 out-of-tree 构建，不得因测试而改变 `upstream/diskann-cpp-main/`。
6. 完成报告必须如实分列：
   - 已执行的测试/验证命令及结果；
   - 失败的测试、失败原因与当前影响；
   - 未执行但适用的测试、未执行原因和剩余风险。

只有必要的校验已完成且通过时，才能声称实现“已通过”、“验证完成”或“可进入下一规模实验”。未运行、因环境阻塞、仅编译成功或尚有适用测试失败时，必须明确写为“未完成验证”。

## 交付前核对

每次交付前确认：

- 未修改 `upstream/diskann-cpp-main/` 或其固定 gitlink。
- Baseline 引用包含 repository、branch 和完整 commit；有效运行还包含 compiler、build flags、dataset checksum 和完整参数。
- Baseline Merge 未被误述为剪枝、refinement 或连通性修复。
- 局部图 ID 空间、LocalToGlobal 映射和所有 Adapter 均显式且可校验。
- Artifact、BuildManifest 和 EvaluationReport 仍然彼此分离。
- 每个 implementation 均有 `requires` 和 `provides`，且所有输入 Artifact 已通过 contract validator。
- 未越过 SIFT1M/人工审批闸门，无静默参数变更或挑选最优运行。
- 最终报告准确记录已执行、失败和未执行的验证，并且不超出证据声称完成。
