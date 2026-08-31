# DiskANN 模块化论文复现框架

本仓库围绕固定的 Microsoft DiskANN `cpp_main` C++ Baseline，提供可插拔、可追溯、可验证的图构建与搜索评估框架。核心构建流水线固定为：

```text
Dataset -> Partition -> Local Build (per partition) -> Merge -> GlobalGraph
```

在这个公共流水线上，可以替换单个论文算法实现；搜索评估是 `GlobalGraph` 之后的独立下游，不是第四个构建 Stage。

固定 Baseline 身份：

```yaml
repository: https://github.com/microsoft/DiskANN.git
branch: cpp_main
commit: 78256bbab4685e1774e78d331e081a153be26823
source_path: upstream/diskann-cpp-main
```

`upstream/diskann-cpp-main/` 始终只读，所有构建、测试、实验代码和输出都必须位于其外部。

## 从哪里开始

- [代码结构与设计说明](docs/architecture/codebase-structure-and-design.md)：仓库分层、模块职责、运行链路、契约和当前实现状态。
- [公共流水线规范](specs/001-modular-cpp-diskann-pipeline/spec.md)：三阶段框架和 Artifact/Manifest/Report 公共语义。
- [固定 cpp_main Baseline](specs/002-cpp-main-upstream-baseline/spec.md)：P0、B0、M0 的源码映射与验证边界。
- [GP-ANN OGP Partition 适配](specs/003-ogp-partition-adaptation/spec.md)：替换 Partition 的论文实现。
- [HNSW-Merger Vamana Merge 适配](specs/004-hnsw-merger-diskann-merge-adaptation/spec.md)：替换 Merge 的论文实现。
- [DiskANN 搜索评估](specs/005-diskann-search-evaluation/spec.md)：PQ、磁盘布局、搜索观测与离线评估。

## 目录速览

```text
include/diskann_pipeline/  公共 C++17 API
src/                       公共内核、Baseline、论文实现、搜索与 runner
contracts/                 运行时 JSON Schema 和契约规则
harness/python/            YAML/JSON 归一化与子进程编排
tests/                      单元、契约、性质、集成和实现专项测试
specs/                      按 Feature 管理的规范、计划、映射与验证记录
experiments/                配置、Manifest、Artifact、Report 和结果分区
upstream/                   固定只读上游源码
```

构建开关、runner 与测试入口随 Feature 不同。执行实验前，请使用对应 Feature 的 `quickstart.md`，不要把通用 `diskann_pipeline_runner` 当成所有真实实现的统一入口。

## 结果解读提醒

“代码已实现”“结构校验通过”和“正式 Baseline/论文结论成立”是三个不同层次。当前公共契约会分别保存 Artifact、BuildManifest、ValidationEvidence 和 EvaluationReport；实验结论必须继续遵守各 Feature 的 Gate、数据规模审批和公平性要求。
