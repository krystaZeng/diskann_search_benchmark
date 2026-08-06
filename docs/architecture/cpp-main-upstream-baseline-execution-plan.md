# cpp_main Upstream Graph Baseline 执行方案

## 1. 文档定位

本文定义固定 Microsoft DiskANN `cpp_main` commit 的第一阶段 Baseline 工作流。

第一阶段只回答三个问题：

1. 固定 upstream 的真实 sharded graph-build 路径是什么；
2. P0、B0、M0 能否在不修改 upstream、不复制算法、不改变参数和随机性的前提下忠实暴露；
3. 如果完整 Unified P0+B0+M0 路径可达，它与 Native sharded Graph Reference 是否通过预先冻结的 Graph Parity Gate。

本文不是完整 DiskANN build/search 平台方案，也不是论文性能评价方案。

固定 Baseline 身份：

```yaml
repository: https://github.com/microsoft/DiskANN.git
branch: cpp_main
commit: 78256bbab4685e1774e78d331e081a153be26823
source_path: upstream/diskann-cpp-main
language: C++
```

Feature short name：

```text
cpp-main-upstream-baseline
```

Feature 目录必须以 Spec Kit 实际创建结果为准。不得把数字编号当作永久语义身份。

## 2. 第一阶段范围

### 2.1 包含

- 固定 commit、gitlink、子模块 HEAD 和 clean 状态验证；
- 真实 CMake target、native entry、参数、默认值、线程条件和随机性研究；
- single-build 与 sharded branch 条件研究；
- P0、B0、M0 的逐符号 source mapping；
- out-of-tree Native Probe；
- Native sharded Graph Reference；
- 可忠实拆分阶段的 Stage Binding；
- Artifact ownership、lineage、checksum、ID 空间和完整输入集合校验；
- frozen Baseline Profile；
- frozen Graph Parity Protocol；
- SIFT1M Graph Parity Gate；
- PASS、FAIL、INCONCLUSIVE 或 not_reached 报告；
- 只有 PASS 后注册 `validated_graph_baseline`。

### 2.2 第一版固定边界

```yaml
study_class: baseline_validation
reproduction_mode: upstream_faithful
replacement_scope: none

data_type: float32
metric: l2
filtered: false
labels: false
build_pq: false
disk_pq: false
opq: false

formal_parity_dataset: SIFT1M
maximum_dataset: SIFT1M
required_branch: sharded
minimum_partitions: 2
```

### 2.3 不包含

- 论文算法或论文性能结论；
- Qdrant 集成；
- 在线插入、删除、更新；
- filtered、label、PQ、OPQ 扩展；
- 其他数据类型或距离类型；
- 超过 SIFT1M 的实验；
- Recall、QPS、搜索延迟正式评价；
- 完整 DiskANN build/search parity；
- Instrumented Baseline；
- upstream 源码埋点或控制流修改；
- Loop Engine、参数矩阵、自动调参、重试系统；
- 通用 Adapter 框架；
- Checkpoint 调度；
- 未经人工批准的公共 Contract 破坏性变更。

## 3. 核心原则

1. upstream 始终只读。构建、运行、日志、临时文件和 Artifact 必须位于 upstream 之外。
2. Source mapping 先于实现。无法由源码或 Native Probe 确认的事实必须标记为 uncertain。
3. Native Probe 先于实现 Gate。不得要求在 Native 运行前证明只有 Native 运行才能证明的事实。
4. P0、B0、M0 分别判断 availability，不采用全有或全无的阶段结论。
5. 完整 Unified Graph Parity 只有在 P0、B0、M0 均可拆分时才可达。
6. 不可观察的阶段不得根据最终 GlobalGraph 反推阶段级 parity。
7. 不复制或重新实现 upstream 算法来制造可拆分性。
8. Contract gap 不得静默修补，但最小可修复 gap 允许经过独立人工批准后修订并重新进入 Gate A。
9. Graph Parity Protocol 必须在正式 SIFT1M Gate 前冻结。
10. upstream 的随机行为不得通过固定 seed 伪装成 Upstream Faithful。
11. PASS 只代表 `validated_graph_baseline`，不代表完整 DiskANN 或搜索性能等价。

## 4. 交付物

### 4.1 始终生成

```text
spec.md
research.md
source-mapping.md
graph-parity-report.json
```

若正式 Gate 未启动，`graph-parity-report.json` 必须记录：

```json
{
  "formal_gate_status": "not_reached",
  "reason": "...",
  "upstream_commit": "78256bbab4685e1774e78d331e081a153be26823",
  "research_ref": "...",
  "source_mapping_ref": "...",
  "affected_stages": []
}
```

### 4.2 Gate A 后按需生成

只要存在可实施的 Native Reference、Stage Binding 或完整 Unified 路径，就可以生成：

```text
plan.md
tasks.md
baseline-run-profile.yaml
```

仅当完整 Unified Graph Parity 可达时生成：

```text
parity-protocol.md
```

### 4.3 条件生成

只有真实证据证明当前公共 Contract 无法表达 Baseline 时生成：

```text
contract-gap.md
```

### 4.4 删除的重复交付物

第一阶段不生成独立 `baseline-lock.json`。

冻结身份由以下对象共同承担：

- `baseline-run-profile.yaml`：固定源码、构建、数据集、参数和 branch 要求；
- BuildManifest：记录实际运行使用的完整身份；
- `parity-protocol.md`：引用 frozen Profile checksum；
- `graph-parity-report.json`：引用 frozen Profile 和 Protocol checksum。

## 5. 状态模型

### 5.1 阶段 availability

P0、B0、M0 分别使用：

```yaml
separability: separable | composite_only | unsupported | contract_gap | uncertain
availability: research_only | planned | implemented | unavailable | validated
unavailable_reason: null | composite_only | unsupported | contract_gap | unobservable | parity_failed | parity_inconclusive
```

### 5.2 Contract 状态

```yaml
contract_status: compatible | minimal_amendment_required | blocking_incompatible
```

- `compatible`：现有 Contract 足以忠实表达目标阶段；
- `minimal_amendment_required`：存在范围清晰、可版本化、不会重写算法语义的最小缺口；
- `blocking_incompatible`：忠实表达需要重大重构、改变阶段语义或引入尚未批准的新模型。

### 5.3 Formal Gate 状态

```yaml
formal_gate_status: not_reached | reached
result: null | PASS | FAIL | INCONCLUSIVE
```

`not_reached` 表示正式 Gate 从未启动，不得使用 INCONCLUSIVE 替代。

## 6. 只保留两个人工 Gate

### 6.1 Gate A：实现前

Gate A 审查：

- `spec.md`；
- `research.md`；
- `source-mapping.md`；
- out-of-tree Native Probe 证据；
- single-build 与 sharded branch 条件；
- P0/B0/M0 的逐阶段 separability；
- 原生中间产物可观察性；
- Contract 状态；
- Native 与 Unified 的 Source/Build Identity 比较原则；
- Graph Parity 的逻辑比较边界。

Gate A 不要求已经实现 Stage Binding，也不要求已有正式 parity 结果。

### 6.2 Gate B：注册前

Gate B 审查：

- frozen Baseline Profile；
- frozen Parity Protocol；
- Profile 和 Protocol checksum；
- 固定 upstream commit；
- Source Identity 和 Build Identity；
- SIFT1M 输入 checksum；
- Native 与 Unified 都进入 sharded branch；
- Partition 数量不少于 2；
- P0、B0、M0 都真实执行；
- upstream 运行前后 clean；
- `graph-parity-report.json` 结果。

只有结果为 PASS 时注册：

```yaml
availability: validated
baseline_scope: validated_graph_baseline
```

路径治理、Checklist、Analyze 和 Converge 都属于普通 Review，不再升级为人工治理 Gate。

## 7. 阶段 0：Feature 路径治理

这是一次普通检查，不是人工 Gate。

在 Codex 中输入：

```text
请只检查并修复新 Baseline Feature 的路径治理问题。

本次不要创建新 Feature，不实现 Baseline，不修改 upstream，
不迁移、不重命名、不重新编号已有：

specs/001-modular-cpp-diskann-pipeline/

新 Feature short name：

cpp-main-upstream-baseline

请完成：

1. 检查 AGENTS.md、README、架构文档、Spec Kit 脚本和模板中，
   是否硬编码了尚未创建的 Baseline Feature 数字路径。
2. Baseline Feature 优先按 short name 定位。
3. 必须使用目录路径时，使用 Spec Kit 实际生成的目录。
4. 不得同时创建两个语义冲突的 Baseline Feature。
5. 不得修改 upstream/diskann-cpp-main。
6. 不得修改 specs/001-modular-cpp-diskann-pipeline/ 的内容或编号。

完成后报告：

- 检查的文件；
- 修改的文件；
- 修改原因；
- 是否仍有冲突路径；
- git diff --check 结果。
```

路径检查通过后直接进入 Specify，无需单独人工 Gate。

## 8. 阶段 1：Specify

运行：

```text
$speckit-specify

创建新的 Feature，short name：

cpp-main-upstream-baseline

Feature metadata：

study_class: baseline_validation
reproduction_mode: upstream_faithful
replacement_scope: none

目标：

为固定 commit 78256bbab4685e1774e78d331e081a153be26823 的
upstream/diskann-cpp-main 建立只读、可审计、可复现的 Graph Baseline。

第一版仅覆盖：

- float32；
- L2；
- unfiltered；
- labels/PQ/OPQ disabled；
- SIFT1M；
- sharded graph-build；
- P0、B0、M0；
- Native Graph Reference 与 Unified Graph Pipeline 的 Graph Parity。

必须：

1. 验证 gitlink、子模块 HEAD、固定 commit 和 upstream clean 状态。
2. 研究真实 CMake target、native entry、参数、默认值、线程条件、
   single/sharded branch、随机性、输出和中间文件。
3. 分别判断 P0、B0、M0 是否 separable。
4. 无法忠实拆分时 fail closed，不复制或重写 upstream 算法。
5. 在实现 Gate 前执行 out-of-tree Native Probe，证明 branch 和产物可观察性。
6. 分别记录阶段 availability；完整 Unified Gate 不采用阶段全有或全无的研究结论。
7. 如果出现 Contract gap，区分 minimal_amendment_required 与 blocking_incompatible。
8. 只有完整 Unified 路径可达时执行正式 Graph Parity。
9. Graph Parity 允许 PASS、FAIL、INCONCLUSIVE；不可启动时记录 not_reached。
10. 只有 PASS 后注册 validated_graph_baseline。
11. upstream 的参数、默认值、随机性和控制流不得改变。
12. 构建、运行、日志和 Artifact 全部位于 upstream 之外。

不包含 Recall、QPS、搜索延迟、完整 DiskANN build/search parity、
论文算法、Loop Engine、Instrumented Baseline 和通用 Adapter。

spec.md 只写需求、领域约束和验收标准，不提前写死：

- C++ 类签名；
- wrapper 目录；
- 完整 JSON Schema；
- 尚未确认的 CMake 命令；
- 尚未确认的源码行号；
- 正式运行次数和统计阈值。
```

## 9. 阶段 2：条件性 Clarify

只有真实歧义会改变 Feature 边界或验收结论时才运行 `$speckit-clarify`。

不得重复询问：

- 固定 commit；
- float32、L2、unfiltered；
- labels/PQ/OPQ disabled；
- SIFT1M 最大规模；
- 正式 Gate 必须进入 sharded branch；
- single-build 不能作为三阶段 parity；
- 不修改 upstream；
- 阶段 separability 由 Research 与 Native Probe 决定；
- 不可观察阶段不能声明阶段级 parity；
- PASS、FAIL、INCONCLUSIVE、not_reached 的区别；
- 第一阶段不做 Recall/QPS。

最多澄清：

1. Graph Parity 的逻辑比较边界；
2. composite implementation 是否只作为 Native Reference；
3. partial Stage Binding 是否值得实现；
4. minimal Contract Amendment 的批准边界；
5. not_reached 报告的最小内容。

需要源码或运行才能回答的问题必须标记为 research question。

## 10. 阶段 3：只读 Research 与 Native Probe

### 10.1 Research 输入

优先只读查看：

```text
upstream/diskann-cpp-main/src/disk_utils.cpp
upstream/diskann-cpp-main/src/partition.cpp
upstream/diskann-cpp-main/include/partition.h
upstream/diskann-cpp-main/include/disk_utils.h
upstream/diskann-cpp-main/include/index.h
upstream/diskann-cpp-main/CMakeLists.txt
upstream/diskann-cpp-main/README.md
```

允许只读查看确认调用链、构建、模板实例化、文件格式和随机性所需的其他 upstream 文件。

### 10.2 Native Probe 定义

Native Probe 是 Gate A 前允许的最小实证步骤。

允许：

- 在 upstream 外进行 out-of-tree configure/build；
- 运行固定 upstream 的原生入口；
- 使用不超过 SIFT1M 的探索性数据；
- 使用探索性 RAM budget 验证 single/sharded branch 条件；
- 记录实际命令、日志、参数、默认值和 Build Identity；
- 观察中间文件生成、重命名、读取和删除；
- 在不改变 upstream 行为的情况下复制即将被原生清理的文件；
- 验证运行前后 upstream clean。

禁止：

- 修改 upstream；
- 实现 Stage Binding；
- 修改公共 Contract；
- 给 upstream 加埋点；
- 固定随机 seed；
- 把 Probe 当作正式 parity；
- 注册 Baseline；
- 输出论文或性能结论。

### 10.3 Codex 指令

```text
请对 cpp-main-upstream-baseline 进行只读源码研究和 out-of-tree Native Probe。

固定 commit：

78256bbab4685e1774e78d331e081a153be26823

不得修改 upstream，不实现 Stage Binding，不提前修改公共 Contract，
不通过源码埋点捕获中间产物，不声称正式 parity。

生成或更新：

- research.md
- source-mapping.md

research.md 必须回答：

1. 实际 CMake target 和原生入口；
2. 固定 Profile 到原生参数的候选映射；
3. R、L、build_DRAM_budget、num_threads 的来源和语义；
4. 当前 Graph Gate 实际消费的其他参数和默认值；
5. 影响 graph-build 的环境变量和线程条件；
6. single-build 与 sharded branch 的真实条件；
7. 如何在不修改 upstream 的情况下证明进入 sharded branch；
8. Partition 数量的决定方式；
9. P0、B0、M0 的真实调用链；
10. B0 实际调用的 Index::build/save 路径；
11. M0 的真实行为和明确不包含的行为；
12. Partition、B0、M0 的随机性；
13. Native 中间文件的生成、读取、重命名和删除生命周期；
14. 哪些阶段产物可观察、可非侵入复制或不可观察；
15. 每个阶段的初步 separability；
16. 现有公共 Contract 的映射结果；
17. Contract 状态是 compatible、minimal_amendment_required
    还是 blocking_incompatible；
18. 哪些字段适合 checksum 身份验证；
19. 哪些输出只能做结构、调用路径或重复运行比较；
20. Native 与 Unified 的 Source/Build Identity 比较原则。

Native Probe 必须记录：

- configure/build/run 命令；
- compiler、CMake、generator、build type 和有效选项；
- 输入数据身份；
- branch evidence；
- Partition 数量；
- 中间文件生命周期；
- upstream 前后状态；
- 失败和未确认项。

无法确认的内容标记 uncertain，不得猜测。
```

## 11. source-mapping.md 要求

`source-mapping.md` 只保存源码事实，不重复保存架构决策。

每个条目至少包含：

```yaml
commit: 78256bbab4685e1774e78d331e081a153be26823
stage: P0 | B0 | M0 | branch | build
symbol: "verified signature or symbol"
path: "repository-relative path"
line_range: "verified range"
caller: "verified caller or unknown"
callee: "verified callee or unknown"
status: verified | uncertain | renamed | not_found
native_inputs: []
native_outputs: []
artifact_observability: observable | copyable_non_intrusively | unobservable | uncertain
evidence: "concise source fact"
```

`research.md` 保存：

- 归纳结论；
- separability；
- Contract 状态；
- Parity 可达性；
- 实现影响；
- 未决问题。

## 12. Gate A：实现前人工审查

Gate A 必须为每个阶段分别记录：

```yaml
stage_decisions:
  P0:
    separability: separable | composite_only | unsupported | contract_gap | uncertain
    artifact_observability: observable | copyable_non_intrusively | unobservable | uncertain
    contract_status: compatible | minimal_amendment_required | blocking_incompatible
  B0: {}
  M0: {}

native_reference_reachable: yes | no
unified_graph_parity_reachable: yes | no
```

### 12.1 路径 A：完整 Unified Gate 可达

只有同时满足以下条件时：

- P0、B0、M0 均为 separable；
- 所需原生产物均 observable 或 copyable_non_intrusively；
- Contract 均 compatible；
- Native Reference 可运行；

才设置：

```yaml
unified_graph_parity_reachable: yes
```

随后进入完整 Plan、Tasks、Bindings 和 Graph Parity。

### 12.2 路径 B：需要最小 Contract Amendment

任一阶段为 `minimal_amendment_required` 时：

1. 生成 `contract-gap.md`；
2. 暂停受影响 Stage Binding；
3. 不静默修改 Contract；
4. 由人工决定创建独立 Contract Amendment 任务或 Feature；
5. 完成版本、schema、validator、Registry capability、Adapter 影响和测试更新；
6. 重新执行受影响的 Gate A 检查；
7. 通过后恢复 Baseline Feature。

这一路径不是 Feature 永久失败，也不得绕过 Contract 审查。

### 12.3 路径 C：只支持部分 Stage Binding

如果只有部分阶段 separable：

- Native Reference 仍可继续；
- 可为已确认 separable 的阶段制定有限 Plan 和 Tasks；
- 不得执行完整 Unified P0+B0+M0 Parity；
- 不得注册 `validated_graph_baseline`；
- 不得把 composite_only 阶段伪造成独立实现；
- `graph-parity-report.json` 记录 `formal_gate_status: not_reached`；
- 报告同时保留已验证阶段的 availability 和证据。

### 12.4 路径 D：阻塞或不可验证

如果 Native Reference 不可运行，或关键阶段为 blocking_incompatible、unsupported、unobservable：

- 受影响实现标记 unavailable；
- 不生成伪造的 Unified Pipeline；
- 不启动正式 SIFT1M Gate；
- 输出 not_reached 报告；
- Feature 以可信的受限结论结束。

## 13. contract-gap.md 最小结构

```md
# Contract Gap

## Evidence

- fixed upstream commit
- source path and symbol
- Native Probe evidence

## Affected stage

P0 | B0 | M0

## Current contract

当前 Contract 能表达什么。

## Native fact that cannot be represented

无法忠实表达的真实行为或产物。

## Classification

minimal_amendment_required | blocking_incompatible

## Minimal proposed amendment

只描述最小变更，不直接修改 Contract。

## Versioning and impact

- schema/version
- validator
- Registry requires/provides
- Adapter
- tests
- backward compatibility

## Stop condition

在人工批准前哪些实现不得继续。
```

## 14. 阶段 4：Plan

只有 Gate A 明确了可实施范围后运行 `$speckit-plan`。

```text
$speckit-plan

请基于人工批准的：

- spec.md
- research.md
- source-mapping.md
- Native Probe evidence
- Gate A stage decisions
- 已批准的最小 Contract Amendment（如有）

制定 cpp-main-upstream-baseline 的最小实现方案。

要求：

1. upstream 始终只读，所有 build/run/output 位于 upstream 外。
2. 只实现 Gate A 批准的 Native Reference 和 separable Stage Binding。
3. 不复制或重写 upstream 算法。
4. Stage Binding 调用固定 upstream 的真实 target、entry 或 callable。
5. 不要求 Native Runner、Stage Binding、格式转换和 provenance
   分别成为独立类、进程或 executable。
6. 只有真实格式不兼容且 Contract 已批准时才创建最小 Adapter。
7. 不根据最终 GlobalGraph 反推不可观察阶段 parity。
8. 不静默修改 Contract。
9. 完整 Unified Gate 不可达时，Plan 必须明确受限范围和 not_reached 结论。
10. 不建设完整 DiskANN build/search 平台。

完整 Unified Gate 可达时，实现顺序：

Native Reference
→ draft Baseline Profile
→ Stage Bindings
→ exploratory runs
→ frozen Baseline Profile
→ frozen Parity Protocol
→ formal SIFT1M Graph Gate
→ Graph Parity Report

Build Identity 必须覆盖：

- upstream source tree identity；
- upstream target identity；
- framework/binding source identity；
- compiler identity；
- build type；
- 影响算法或 ABI 的有效编译选项；
- Native executable/callable identity；
- Unified Binding 实际调用或链接的 upstream target identity。
```

## 15. Baseline Profile

### 15.1 生命周期

```text
draft → frozen → superseded
```

- `draft`：由 Research 和 Native Probe 生成，用于探索；
- `frozen`：所有正式 Graph Gate 参数、默认值、数据集和 Build Identity 确认后冻结；
- `superseded`：需要变化时创建新版本，旧 frozen Profile 不原地修改。

### 15.2 最小结构

```yaml
schema_version: "1.0"

profile:
  id: cpp-main-graph-f32-l2-unfiltered-v1
  version: 1
  status: draft
  scope: graph_stage

upstream:
  repository: https://github.com/microsoft/DiskANN.git
  branch: cpp_main
  commit: 78256bbab4685e1774e78d331e081a153be26823
  source_tree_checksum: "<verified>"
  target_identity: "<verified>"

binding:
  repository: "<verified>"
  commit: "<verified>"
  dirty_worktree: false

build:
  compiler: "<verified>"
  compiler_version: "<verified>"
  cmake_version: "<verified>"
  generator: "<verified>"
  build_type: "<verified>"
  effective_options: []

dataset:
  name: sift1m
  data_type: float32
  metric: l2
  checksum_algorithm: sha256
  checksum: "<verified>"
  global_node_count: 1000000
  vector_dimension: 128

features:
  filtered: false
  labels: false
  build_pq: false
  disk_pq: false
  opq: false

graph_build:
  R: "<verified>"
  L: "<verified>"
  build_dram_budget: "<verified>"
  num_threads: "<verified>"
  effective_parameters: {}
  effective_defaults: {}

execution:
  required_branch: sharded
  minimum_partitions: 2

profile_checksum:
  algorithm: sha256
  value: "<computed>"
```

正式 Gate 不接受包含 TBD、unknown、空必填字段或 draft 状态的 Profile。

## 16. Parity Protocol

`parity-protocol.md` 只在完整 Unified Gate 可达且 Profile 已 frozen 后生成和冻结。

必须引用：

- Profile ID、version 和 checksum；
- fixed upstream commit；
- SIFT1M checksum；
- Source/Build Identity 比较规则。

必须预先定义：

1. Native 与 Unified 的输入；
2. 两条路径的调用入口；
3. sharded branch 证据；
4. Partition 数量要求；
5. P0/B0/M0 执行证据；
6. checksum 只用于哪些身份和完整性字段；
7. 哪些输出允许精确比较；
8. 哪些输出因 upstream 随机性只能比较结构、调用路径或重复运行结果；
9. 正式运行次数；
10. PASS、FAIL、INCONCLUSIVE 判定；
11. 无效运行和排除规则；
12. upstream clean 检查；
13. Report 必须保存的证据。

不得默认要求随机图输出 byte-identical。M0 的随机打乱、多线程 B0 或 Partition 随机性必须按固定源码事实处理。不得通过固定 seed 改变 Upstream Faithful 语义。

## 17. 阶段 5：Tasks

运行 `$speckit-tasks`，只生成三个实施 Phase。

### Phase 1：Native Reference 与 draft Profile

- 验证 gitlink、HEAD、commit、source tree 和 clean 状态；
- 固化 out-of-tree build/run；
- 实现固定 upstream Native Graph Reference；
- 固化 single/sharded 分类证据；
- 验证至少两个 Partition；
- 记录 Graph 参数和有效默认值；
- 生成 draft Profile；
- 记录 Native Build Identity；
- 添加相应测试；
- 不冻结 Profile；
- 不生成 Protocol；
- 不执行正式 SIFT1M Gate。

### Phase 2：已批准的 Faithful Stage Bindings

- 只实现 Gate A 判定为 separable 的阶段；
- B0 使用明确 PartitionId 和正确 Dataset/Partition lineage；
- M0 验证完整 PartitionArtifact 和 LocalGraphArtifact 集合；
- 验证 ownership、checksum、格式版本和 ID 空间；
- 调用固定 upstream 的真实实现；
- 不复制算法；
- 不改变参数、默认值或随机性；
- 不提前标记 validated；
- 新 Contract gap 必须停止受影响实现并回到 Amendment 路径。

### Phase 3：Exploration、Freeze、Formal Gate

- 用 draft Profile 做少量探索运行；
- 确认参数、默认值、随机性和比较边界；
- 冻结 Profile 并计算 checksum；
- 生成并冻结 Protocol；
- 执行正式 SIFT1M sharded Gate；
- 生成 Report；
- PASS 后注册 validated_graph_baseline；
- FAIL/INCONCLUSIVE 保持 unavailable；
- 上游前后 clean。

每个任务必须写明依赖、输入、输出和适用 Gate。任务编号生成后保持稳定。

## 18. 阶段 6：Analyze

在实现前运行一次 `$speckit-analyze`。

重点检查：

1. spec 是否保持需求层；
2. Plan 是否完全基于 Research、Native Probe 和 Gate A；
3. 是否假定阶段必然可拆分；
4. 是否复制或重写 upstream；
5. 是否把 Probe 当成正式 parity；
6. 是否强制实现不可拆分阶段；
7. 是否从最终图反推不可观察阶段 parity；
8. 是否静默修改 Contract；
9. 是否保留 minimal Contract Amendment 恢复路径；
10. 是否逐阶段记录 availability；
11. 是否在探索前冻结 Profile；
12. Protocol 是否引用 frozen Profile checksum；
13. Report 是否引用 Profile 和 Protocol；
14. 是否误将 single-build 当作三阶段证据；
15. 是否纳入 Source/Build Identity；
16. 是否允许 PASS、FAIL、INCONCLUSIVE 和 not_reached；
17. 是否在未 PASS 时注册 Baseline；
18. 是否错误加入 Recall/QPS、Loop Engine 或通用 Adapter。

只报告问题，不修改代码。按 CRITICAL、HIGH、MEDIUM、LOW 分级。

## 19. 阶段 7：分 Phase 实现

### 19.1 Implement Phase 1

```text
$speckit-implement

只执行 tasks.md 的 Phase 1：Native Reference 与 draft Profile。

只实现：

- upstream identity/clean guard；
- out-of-tree build/run；
- Native Graph Reference；
- single/sharded branch evidence；
- Graph 参数和默认值记录；
- draft Profile；
- Native Build Identity；
- 对应测试。

不得实现 Stage Binding，不冻结 Profile，不生成 Protocol，
不开始正式 SIFT1M Gate，不注册 Baseline，不修改 upstream。

完成后报告修改文件、真实命令、测试、branch evidence、
Partition 数量、draft Profile、Build Identity、upstream 状态和未完成任务。
```

### 19.2 Implement Phase 2

```text
$speckit-implement

只执行 tasks.md 的 Phase 2：Gate A 批准的 Faithful Stage Bindings。

要求：

- 只实现 separable 阶段；
- B0 接收明确 PartitionId 和正确 lineage；
- M0 接收完整、逐 Partition 对应的 LocalGraphArtifact 集合；
- 验证 ownership、lineage、checksum、format version 和 ID space；
- 调用固定 upstream 的真实实现；
- 不复制算法、不修改 upstream、不改变参数和随机性；
- 不根据最终图推断阶段 parity；
- 不提前标记 validated。

出现新 Contract gap 时停止受影响实现，生成 contract-gap.md，
回到人工批准的 Amendment 路径。

完成后分别报告 P0/B0/M0 的状态、输入输出、原生证据、
Contract 映射、测试结果和完整 Unified Gate 是否仍可达。
```

如果 Phase 2 证明完整 Unified 路径不可达，直接生成 not_reached Report，不执行 Phase 3 正式 Gate。

### 19.3 Implement Phase 3

```text
$speckit-implement

只执行 tasks.md 的 Phase 3：Exploration、Freeze 和 Formal Graph Gate。

前提：

- P0、B0、M0 均 separable；
- 所需产物均可观察或可非侵入复制；
- 无未解决 Contract gap；
- Native Reference 和所有 Binding 测试通过。

顺序不得改变：

1. 使用 draft Profile 做少量探索运行；
2. 确认参数、默认值、随机性、可精确比较字段、
   结构比较字段、正式运行次数和无效运行规则；
3. 冻结 baseline-run-profile.yaml 并计算 checksum；
4. 生成并冻结 parity-protocol.md；
5. 执行正式 SIFT1M Graph Gate；
6. 证明两条路径使用相同输入身份和 frozen Profile；
7. 证明两条路径进入 sharded branch且 Partition 数量不少于 2；
8. 证明 P0、B0、M0 真实执行；
9. 验证 Source Identity、Build Identity 和 upstream clean；
10. 生成 graph-parity-report.json；
11. PASS 后注册 validated_graph_baseline；
12. FAIL/INCONCLUSIVE 保持 unavailable。

不得固定 seed，不得为获得 PASS 修改参数，不得丢弃不利的有效运行，
不得声称 Recall/QPS 或完整 DiskANN 等价。
```

## 20. Graph Parity Report

### 20.1 正式 Gate 已执行

```json
{
  "formal_gate_status": "reached",
  "result": "PASS",
  "profile": {
    "id": "cpp-main-graph-f32-l2-unfiltered-v1",
    "version": 1,
    "checksum": "..."
  },
  "parity_protocol_checksum": "...",
  "upstream_commit": "78256bbab4685e1774e78d331e081a153be26823",
  "dataset_checksum": "...",
  "native_build_identity": {},
  "unified_build_identity": {},
  "branch_evidence": {},
  "stage_evidence": {
    "P0": {},
    "B0": {},
    "M0": {}
  },
  "comparisons": {},
  "valid_runs": [],
  "invalid_runs": [],
  "upstream_clean_before": true,
  "upstream_clean_after": true
}
```

`result` 可以是 PASS、FAIL 或 INCONCLUSIVE。

### 20.2 正式 Gate 未执行

```json
{
  "formal_gate_status": "not_reached",
  "reason": "separability_precondition_failed",
  "upstream_commit": "78256bbab4685e1774e78d331e081a153be26823",
  "research_ref": "research.md",
  "source_mapping_ref": "source-mapping.md",
  "affected_stages": ["M0"],
  "stage_decisions": {},
  "contract_status": {}
}
```

合法 `reason` 至少包括：

- `native_reference_unavailable`；
- `separability_precondition_failed`；
- `intermediate_artifact_unobservable`；
- `blocking_contract_gap`；
- `unsupported_native_stage`。

## 21. Gate B：注册前人工审查

Gate B 只接受正式 Gate 已执行的报告。

### PASS

```yaml
availability: validated
baseline_scope: validated_graph_baseline
```

允许：

- P0/B0/M0 Stage Binding 验证；
- Graph Artifact 结构验证；
- graph-stage 论文 pilot；
- 使用 frozen Profile 进行 P1+B0+M0、P0+B1+M0、P0+B0+M1 调试；
- 对同一阶段输入进行严格复用和公平比较。

### FAIL

```yaml
availability: unavailable
unavailable_reason: parity_failed
```

### INCONCLUSIVE

```yaml
availability: unavailable
unavailable_reason: parity_inconclusive
```

无论结果如何，都不得声称：

- `validated_diskann_baseline`；
- 完整 DiskANN build/search 等价；
- Recall 等价；
- QPS 或延迟等价；
- 论文方法性能优于 Baseline。

## 22. Converge 使用规则

`$speckit-converge` 不作为默认固定阶段。

仅在以下情况运行一次：

- 实现与 spec/plan/tasks 出现已知偏差；
- Analyze 或验证报告指出缺失任务；
- Contract Amendment 后需要重新核对实现范围；
- Gate B 前发现必要证据未落盘。

Converge 只能追加完成当前 Graph Baseline 所必需的任务，不得加入 Recall/QPS、Loop Engine、通用 Adapter、Instrumented Baseline 或其他数据类型。

只有确实追加任务时，再运行一次 `$speckit-implement` 完成新增任务。不得固定进行多轮循环。

## 23. 第一阶段成功标准

### 23.1 所有路径都必须满足

1. 固定 commit、gitlink、HEAD 和 upstream clean 状态有证据；
2. source-mapping 可审计；
3. single-build 与 sharded branch 条件明确；
4. P0、B0、M0 分别有证据支持的 separability 结论；
5. 每个阶段的原生产物可观察性明确；
6. Contract 状态明确；
7. Native Probe 和正式 Gate 不混淆；
8. 未修改 upstream；
9. 未超过 SIFT1M；
10. 未输出超出 Graph Baseline 范围的结论。

### 23.2 完整 Unified Gate 可达时

1. Native sharded Graph Reference 可复现；
2. P0、B0、M0 Binding 忠实调用固定 upstream；
3. frozen Profile 完整且无未知必填项；
4. Protocol 在正式运行前冻结；
5. 正式 SIFT1M Gate 进入 sharded branch；
6. Partition 数量不少于 2；
7. Report 为 PASS、FAIL 或 INCONCLUSIVE；
8. 只有 PASS 注册 validated_graph_baseline。

### 23.3 完整 Unified Gate 不可达时

1. Report 使用 `formal_gate_status: not_reached`；
2. 具体原因和受影响阶段明确；
3. partial Stage Binding 与 unavailable 阶段分别记录；
4. 未伪造 Unified Pipeline；
5. 未注册 validated_graph_baseline。

## 24. 最终执行顺序

```text
路径治理普通检查
        ↓
$speckit-specify
        ↓
条件性 $speckit-clarify
        ↓
只读 Research + source-mapping
        ↓
out-of-tree Native Probe
        ↓
Gate A：
逐阶段 separability + artifact observability + contract status
        ↓
┌──────────────────────────────────────────────┐
│ minimal Contract Amendment                  │
│ → contract-gap.md                           │
│ → 人工批准并版本化修订                       │
│ → 重新执行受影响的 Gate A 检查              │
└──────────────────────────────────────────────┘
        ↓
┌──────────────────────────────────────────────┐
│ 完整 Unified 路径不可达                      │
│ → 保留 Native Reference/partial Binding 证据 │
│ → formal_gate_status: not_reached            │
│ → 不注册 validated_graph_baseline            │
└──────────────────────────────────────────────┘

或

┌──────────────────────────────────────────────┐
│ P0/B0/M0 均 separable                        │
│ 产物可观察，Contract compatible              │
└──────────────────────────────────────────────┘
        ↓
$speckit-plan
        ↓
$speckit-tasks
        ↓
$speckit-analyze
        ↓
Implement Phase 1：Native Reference + draft Profile
        ↓
Implement Phase 2：Faithful Stage Bindings
        ↓
Implement Phase 3：
Exploration
→ frozen Profile
→ frozen Protocol
→ Formal SIFT1M Graph Gate
→ Graph Parity Report
        ↓
Gate B：只有 PASS 注册 validated_graph_baseline
        ↓
仅在有实际偏差时运行 Converge
        ↓
graph-stage paper pilot
```

## 25. 下一阶段边界

第一阶段 PASS 之后，才能为论文单阶段替换建立下一 Feature。

下一阶段至少补充：

- 任意阶段起跑和上游 Artifact 复用；
- Merge 对比复用同一 PartitionArtifact 和 LocalGraphArtifact 集合；
- Recall@K；
- QPS、p50/p95/p99 latency；
- 构建时间和峰值内存；
- 固定 query、ground truth、线程、缓存和计时范围；
- Exact reproduction、Faithful adaptation、Inspired implementation 分类。

Graph Baseline PASS 本身不能替代这些论文评价要求。
