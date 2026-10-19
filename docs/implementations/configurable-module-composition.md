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

以下 ID 均保留实现版本 `1.0.0`；组合入口额外注册相同 ID 的 P0/B0 `1.1.0`。
简称仅用于说明：

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

P0/B0 `1.0.0` 仍冻结 R=16、B0 L=32。`1.1.0` 允许 P0 的 R、B0 的 R/L
显式配置：R 必须是 `[2, 4294967295]` 内的 JSON 整数，L 必须是
`[1, 4294967295]` 内的 JSON 整数；先检查类型/范围，再用 uint64 计算
`floor(2R/3)` 并检查非零及转换范围。两版共用原生执行和产物生成路径；入口及工厂
按所选版本调用同一个解析器，不按参数值猜测版本。旧注册函数仍只注册三个 `1.0.0`
实现，旧专用入口、schema 和配置保持不变。新版 B0 绑定相同的 `b0_input_admission`。
参数关系不增加上游未要求的 L/R 限制；数值可表示也不代表资源足够。

P0 仍使用 0.6 GiB 构图预算、双副本、`min(1, 256000/N)` 抽样及原生随机性，
按预算确定分块数。B0 仍使用线程数 6、alpha=1.2、候选上限 750、关闭 PQ/OPQ 和
saturation。新版 R=64、L=100 时局部度数上限为 42；有效 filter_list_size 随 L 为 100。
M0 保留 `1.0.0` 和原算法，R 独立配置。公共兼容性检查不要求所有阶段 R 相等。

## 配置与执行

### 准入已有原生数据

现有 Harness 提供 `admit-dataset`，只读取已有的小端原生矩阵：两个 uint32 字段
`N, dimension`，随后连续 `N × dimension` 个 IEEE-754 float32；距离语义为 L2。
不接受原始 fvecs/bvecs，不下载或转换数据。声明文件为 JSON，例如下面的 **96×4 小数据**
声明；文件必须真实存在且与声明一致：

```json
{
  "path": "/absolute/path/small-vectors.bin",
  "dataset_id": "my-small-native-v1",
  "global_node_count": 96,
  "vector_dimension": 4,
  "scalar_type": "float32",
  "distance_metric": "l2"
}
```

可选字段为 `checksum`（预期小写 SHA-256）和 `byte_length`。省略 checksum 时仍完整计算
并记录 SHA-256；提供时必须匹配。相对输入路径以仓库根（或 `--source-dir`）为基准且不得
逃逸，绝对路径可指向已有外部数据。N 和维度必须与实际文件头一致；精确长度必须为
`8 + 4*N*dimension`，所有数值必须有限。保留 uint32 头部和 uint64 长度范围检查。

```sh
.venv/bin/python -m diskann_harness.cli admit-dataset \
  --config /absolute/path/dataset.json --workspace /absolute/path/workspace

# 使用上述 JSON 输出中的 artifact_ref_path；该路径相对于 workspace。
.venv/bin/python -m diskann_harness.cli validate \
  --artifact-ref /absolute/path/workspace/manifests/dataset-admission/<identity>/artifact-ref.json \
  --workspace /absolute/path/workspace \
  --runner build/composition/src/cli/diskann_pipeline_runner
```

准入不启动流水线，不需要 `--runner`。输出继续使用 `diskann-float32-dataset/v1`：
`normal/datasets/<identity>/` 保存不可覆盖的 payload 和 metadata；
`manifests/dataset-admission/<identity>/` 保存独立 ArtifactRef、扫描校验记录及准入来源记录。
来源记录包含输入路径、原声明、实际头部/长度、输入和发布副本的 SHA，以及准入模块源码 SHA。
`<identity>` 由 dataset_id 和实际 payload SHA 派生，不取调用者给出的目录名。
源文件不修改；复制后的 SHA 再次核对，输入不合法、路径越界或目标已存在时退出码为 2。
发布不提供多文件事务或自动覆盖重试；中途 I/O 失败可保留已写入文件，需检查后选择新工作区。

公共 `validate` 根据 Dataset 的明确格式/版本选择原生或 Fake 契约，未知版本拒绝；
底层仍调用已有 C++ validator，包含原生文件校验，不仅是 JSON schema 校验。
准入输出中的 `artifact_ref` 可直接填入组合配置的 `dataset`，并设置对应 workspace。
流水线开始前仍会独立校验该 Artifact。原冻结 SIFT1M preflight 继续使用原 schema、固定
checksum、路径和实验约束；这个公共命令不改变冻结入口的适用范围或认证结论。

### 运行完整组合

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

追加示例 [`p0-b0-m0-default-rl.json`](../../experiments/configs/module-composition/p0-b0-m0-default-rl.json)
和 [`ogp-b0-m0-default-rl.json`](../../experiments/configs/module-composition/ogp-b0-m0-default-rl.json)：
P0 `1.1.0` 使用 R=64，两组 B0 `1.1.0` 使用 R=64/L=100，两组 M0 `1.0.0`
使用 R=64/use_filters=false，线程预算和 B0 线程数均为 6。这里采用的是**上游默认 R/L**，
不是全部上游默认参数；两组 B0/M0 参数一致由这两个配置保证。
DatasetRef 仍对应上述 96×4 fixture，须先按上述 `--prepare` 命令准备工作区；
实际数据须替换完整 ArtifactRef、workspace.root 及 P0 的抽样概率，不能只改路径。
用相应 JSON 替换上述 execute 的 `--request`，并使用不同的新结果文件名即可执行。
OGP 还需启用 `DISKANN_GP_ANN_OGP=ON` 并准入固定源码及匹配的原生 Partition 构建。
请求簇数 4 仅用于此小数据示例；SIFT1M 正式请求值仍待 P0 实际分块结果确定。
新版及其新参数配置不继承旧冻结配置的认证结论。

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
选择新版 P0/B0 时，entry-build 的 parameters 和父 Manifest 的 source_config 另记
`native_effective_parameters`（由同一解析器推导）；结果的 `native_parameter_observations`
记录 P0 传入的度数、B0 实际原生 builder 的度数/L/filter_list_size 及保存后校验上限。
这些记录不进入 Artifact，也不声明质量或性能结论。局部 Artifact 的 declared_max_degree
与保存后校验上限均为 `floor(2R/3)`，observed_max_degree 可以更小。
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

入口不再无条件拒绝超过 1,000,000 节点的数据；规模取自完整校验后的 Artifact 和原生文件头。
这只是技术准入范围的调整。超过 SIFT1M 的实际任务仍须按 AGENTS.md 获得明确人工批准，
配置存在或数据准入成功不代表运行许可；没有新增审批字段或绕过模块资源约束。
只运行完整三阶段，不支持阶段停止、矩阵、批量调度、自动重试或动态插件。
Partition 后会继续 Local Build、Merge 和原有结构评估，预算必须覆盖完整流程。
例如 M1 的向量缓存需要 `4*N*dimension + N + 8*dimension` 字节，另需图和其他工作空间；
不能把现有 1 GiB 小数据配置直接当作大数据预算。

公共准入改动已使用真实 96×4 float32 数据验证 P0+B0+M0、KMeans+B0+M0、OGP+B0+M0，
检查映射、产物、Manifest、独立校验及不可覆盖行为。P0 保持每点双副本，KMeans K=3
实际为 3 块，OGP requested_num_clusters=4、overlap=0.2 实际为 5 块。
本次未执行 SIFT1M 或 SIFT10M 流水线，不构成大规模或性能验证；OGP 固定源码的已知
备用分桶疑点仍须独立核查，本次未复现、修复或修改固定源码。

此前组合入口小数据验收及实际证据见
[验证记录](../../experiments/reports/module-composition-small-20261014/verification.json)。
96×4 float32 fixture 已通过新入口执行 P0+B0+M0、KMeans+B0+M0、OGP+B0+M0、
KMeans+B0+M1、P0+B1+M0。KMeans 使用 K=3、迭代上限默认 15；OGP 使用
requested_num_clusters=4、requested_overlap=0.2、default；B1 使用 omega=8、lambda=16、
alpha=1.2、seed=42、iteration_cap=100，完整策略字符串保存在各次请求和结果中。
只有记录为通过的组合/配置完成原生功能和结构验证；候选模块的其他组合不自动成为已验证。
混合组合以及新入口下的 P0+B0+M0 都不自动继承 Baseline 的 CERTIFIED。
结构验证不代表 Recall/QPS、性能或论文效果验证；未自动运行 SIFT1M/SIFT10M。
紧凑性仍须按其独立说明显式调用。
