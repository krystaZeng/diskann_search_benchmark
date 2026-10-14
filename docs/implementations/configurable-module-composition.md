# 已有模块的配置化组合入口

`module_composition_runner` 显式读取一个配置，装配现有 production Registry，调用现有
`PipelineExecutor` 完成 Partition → Local Build → Merge。它不修改 Feature 001 的 Fake
配置范围，不替换任何专用入口，不定义新的算法或认证身份。

## 构建和候选实现

当前入口要求 Linux、固定 DiskANN 的原生依赖以及 CMake Unix Makefiles；构建目录必须
位于上游之外。最小构建只需要 Baseline，不要求安装 GP-ANN：

```sh
cmake -S . -B build/composition -G 'Unix Makefiles' \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON -DDISKANN_MODULE_COMPOSITION=ON
cmake --build build/composition --target module_composition_tests --parallel 4
ctest --test-dir build/composition -R '^module_composition_requests$' --output-on-failure
ctest --test-dir build/composition -R '^module_composition_native$' --output-on-failure
```

需要其他模块时，在同一配置命令中显式启用对应选项，重新配置、构建受影响目标。
离线环境可指定 `-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/绝对路径/已有源码`。
不下载或自动寻找论文源码；OGP 需要 `-DGPANN_SOURCE_DIR=/绝对路径/已核验的gp-ann源码`，
以及其固定子模块和原生依赖。未选择 OGP 时不会运行 OGP 的源码/可执行文件准入。

以下 ID 均使用实现版本 `1.0.0`，简称仅用于说明：

| 阶段/简称 | implementation_id | 构建选项 |
|---|---|---|
| Partition P0 | `cpp_main_78256bbab4685e1774e78d331e081a153be26823_p0_callable_v1` | `DISKANN_CPP_MAIN_BASELINE` |
| Partition KMeans | `diskann_78256bbab4685e1774e78d331e081a153be26823_kmeans_configurable_k_v1` | `DISKANN_KMEANS_PARTITION` |
| Partition OGP | `gp_ann_ogp_partition_v1` | `DISKANN_GP_ANN_OGP` |
| Local Build B0 | `cpp_main_78256bbab4685e1774e78d331e081a153be26823_b0_callable_v1` | `DISKANN_CPP_MAIN_BASELINE` |
| Local Build B1 | `xn_graph_xn_descent_local_build_v1` | `DISKANN_XN_DESCENT` |
| Merge M0 | `cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1` | `DISKANN_CPP_MAIN_BASELINE` |
| Merge M1 | `hnsw_merger_vamana_merge_v1` | `DISKANN_HNSW_MERGER` |

候选名单不是组合白名单。所选 ID、版本和阶段必须精确匹配已编译注册项；不会回退。
实际组合由原有 descriptor、兼容性检查、实际运行时服务、input_admission 和产物校验决定。
B0/B1 使用绑定已校验 Dataset 的原生向量访问器；距离服务明确提供平方 L2；M1 的数据集
绑定要求及 M0/M1 的原生映射/图格式检查继续生效。

## 配置与执行

例子：[`p0-b0-m0.json`](../../experiments/configs/module-composition/p0-b0-m0.json) 和
[`kmeans-b0-m1.json`](../../experiments/configs/module-composition/kmeans-b0-m1.json)。
它们的 DatasetRef 对应现有 `NativeDatasetFixture` 的 96×4 float32 小数据，不能拿该
checksum 代替真实数据的身份。工作区必须有完整的 `normal/dataset/artifact.json` 及 payload。
可复用 KMeans 测试工具准备该小 fixture（此命令仅生成 96×4，不适合 SIFT 数据）：

```sh
# 在上述构建中启用 DISKANN_KMEANS_PARTITION=ON、DISKANN_HNSW_MERGER=ON 后：
cmake --build build/composition --target diskann_kmeans_tests module_composition_tests --parallel 4
build/composition/tests/cpp/partition/diskann_kmeans_tests --prepare build/module-composition-demo
# 原生 P0 使用进程的 OpenMP/MKL 设置，与示例的线程预算保持一致。
export OMP_NUM_THREADS=6 MKL_NUM_THREADS=6
# 在 /workspace 为仓库根的 Linux 环境中，示例的 workspace.root 可直接使用；
# 其他根目录须在配置中改成实际绝对路径。
build/composition/src/module_composition/module_composition_runner execute \
  --request experiments/configs/module-composition/p0-b0-m0.json \
  --result build/module-composition-demo/workspace/reports/p0-result.json
build/composition/src/module_composition/module_composition_runner execute \
  --request experiments/configs/module-composition/kmeans-b0-m1.json \
  --result build/module-composition-demo/workspace/reports/kmeans-m1-result.json
```

配置版本为 `module-composition-run/v1`，顶层必需且仅允许：`config_version`、`run_id`、
`workspace`、`dataset`、`partition`、`local_build`、`merge`。后三者分别包含字符串
`implementation_id`、`implementation_version` 和对象 `parameters`。不允许用户填写
`partition_id`；执行器按实际分区生成该字段。KMeans 的 K 可只改 `partition.parameters.K`，
迭代上限省略时规范化为 15。B1 的省略 seed 按现有规则规范化为 42；不替其他模块添加 seed。
其余范围、枚举、冻结参数及交叉检查复用模块原有解析器；公共简化 schema 检查不是完整
JSON Schema 引擎。依赖实际局部图/分区大小的限制仍在相应阶段执行。
P0 的 `sample_probability` 必须符合冻结推导 `min(1, 256000/N)`；示例中的 1.0 对应
96 行 fixture。更换 Dataset 时须使用它自己的 ArtifactRef，并按 N 填写该值。
OGP 的 `requested_num_clusters` 是原生请求参数，执行器以实际产物中的分块数调度 B0，
不将该参数强行解释成 KMeans 的精确 K。

`workspace` 仅允许绝对路径 `root`、正整数 `memory_budget_bytes` 和正 uint32
`thread_budget`。选择 B0 时 thread_budget 必须为冻结值 6。内存预算不是原生分配器硬限额；
B1 等模块已有的资源检查保持原样。预算应覆盖整条流水线。run_id 只能包含字母、数字、
`-`、`_`，且必须未被占用。结果必须是该 workspace 的 `reports/` 的直接子文件，
不能使用执行器保留的 `<run_id>-evaluation.json` 名称或模块报告子目录，以新文件写入；
符号链接逃逸、写入固定上游、覆盖已有结果均拒绝。已开始准入的请求会保留
`manifests/<run_id>/request.json` 并占用 run_id，失败后也不会自动重试。

线程预算通过 StageContext 交给现有模块；本入口不新增统一的进程资源隔离。原生 P0 的
OpenMP/MKL 线程设置仍由运行环境提供，执行前应与配置预算一致。本次验证两个环境变量
均为 6，实际容器命令保存在验证证据中；不要仅据配置字段声称线程或内存受到硬限额控制。

退出码：成功 `0`，请求/准入/输出写入失败 `2`，执行器非成功终态 `4`。
失败在 stderr 输出含 `outcome: FAILED`（或执行器原终态）、failure 和调用次数的 JSON；
当请求可解析、结果路径安全且可写时，同时保存结构化结果。无法安全确定输出位置或文件
已经存在时仅输出错误，不覆盖。原生模块可能另有诊断日志，须结合结果文件读取原因。

## 输出、来源与边界

成功结果包含有效配置、实际阶段身份/调用次数、Partition/LocalGraph/GlobalGraph ArtifactRef、
父 BuildManifest 和 validation evidence 的路径及 checksum。执行器原有独立结构评估报告
仍写入 reports；不会自动执行紧凑性评估、Search、PQ 或磁盘布局。

`manifests/<run_id>/entry-build.json` 单独记录新入口二进制 SHA、配置时源码字节 SHA、
所选模块静态库及原生库 SHA、真实编译/链接命令、构建选项、有效配置和预算。Git HEAD
不代替未提交源码身份。运行前核对配置时的源码快照，源码改变后需重新配置及构建。
父阶段 Manifest 通过配置来源绑定该入口构建记录；已有阶段自有执行 Manifest 保留，
两者不互相冒充。分阶段 source 分别使用固定 DiskANN、固定 GP-ANN 或项目内 B1/M1
适配实现的身份。Artifact、BuildManifest、EvaluationReport 保持分离。
入口构建记录的时间是本次准入记录创建时间，实际配置/构建命令和耗时见验证日志；
`configured_compile_database` 保存 CMake 配置的命令集合，不声称其中每个未选目标均已构建。

源码语义依据：[Baseline mapping](../../specs/002-cpp-main-upstream-baseline/source-mapping.md)、
[KMeans](diskann-kmeans-partition.md)、
[OGP mapping](../../specs/003-ogp-partition-adaptation/source-mapping.md)、
[B1 mapping](../../specs/007-xn-descent-local-build/source-mapping.md)、
[M1 mapping](../../specs/004-hnsw-merger-diskann-merge-adaptation/source-mapping.md)。
DiskANN 固定为 `https://github.com/microsoft/DiskANN.git`、`cpp_main`、
`78256bbab4685e1774e78d331e081a153be26823`。M0 仍是映射、聚合、去重、随机打乱和截断，
不是距离剪枝或连通性修复。

新入口拒绝声明超过 1,000,000 节点的数据，随后仍完整校验 Artifact 和 native header。
这是本入口的实验准入边界，不是算法容量结论，无大规模绕过参数。
只运行完整三阶段，不支持阶段停止、矩阵、批量调度、自动重试或动态插件。

本轮小数据验收及实际证据见
[验证记录](../../experiments/reports/module-composition-small-20261014/verification.json)。
96×4 float32 fixture 已通过新入口执行 P0+B0+M0、KMeans+B0+M0、OGP+B0+M0、
KMeans+B0+M1、P0+B1+M0。KMeans 使用 K=3、迭代上限默认 15；OGP 使用
requested_num_clusters=4、requested_overlap=0.2、default；B1 使用 omega=8、lambda=16、
alpha=1.2、seed=42、iteration_cap=100，完整策略字符串保存在各次请求和结果中。
只有记录为通过的组合/配置完成原生功能和结构验证；候选模块的其他组合不自动成为已验证。
混合组合以及新入口下的 P0+B0+M0 都不自动继承 Baseline 的 CERTIFIED。
结构验证不代表 Recall/QPS、性能或论文效果验证；未自动运行 SIFT1M/SIFT10M。
紧凑性仍须按其独立说明显式调用。
