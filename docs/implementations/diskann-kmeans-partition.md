# 可配置 K 的原生 DiskANN Partition

本模块身份为 `diskann_78256bbab4685e1774e78d331e081a153be26823_kmeans_configurable_k_v1`（实现版本 `1.0.0`）。它采用固定原生 KMeans 算法及本项目的参数策略，不是冻结 P0，也不继承 P0 的认证结论。公共产物仍为 `partition-artifact/v1`。

## 参数和能力

参数 schema：[`parameters.schema.json`](../../contracts/partition/diskann-kmeans-v1/parameters.schema.json)。

| 参数 | 规则 |
|---|---|
| `K` | 必填正整数，`K <= N`；N 来自校验后的 DatasetArtifact 和原生文件头 |
| `max_k_means_reps` | 正整数，省略时为 15；原生 Lloyd 收敛条件仍可提前结束 |
| 内部抽样概率 | `min(1.0, 256000.0 / N)`；256000 是期望训练样本目标，不是精确样本数或数据集规模上限 |
| 内部分配数 | `k_base=1`，硬分块，每个点恰好属于一个分块 |

不接受 seed、采样策略、重复分配数等额外参数。保留 `random_device` 随机性，不承诺逐字节重放。实际训练样本必须非零、至少 K 个且不超过 8,388,608；检查位于质心初始化之前。失败不重抽样、不减少 K。空分块明确失败，不补点或重新运行。

需要 `diskann-float32-dataset/v1`、`dataset.diskann_float32_bin/v1` 及 cancellation/metrics 服务。提供 `partition.centroids/v1`、`partition.complete_local_to_global_map/v1`、`partition.local_to_global_ascending/v1`，**不提供 `partition.overlap/v1`**。B0 的原有能力要求支持此组合，实际服务和原生映射编码仍由现有 admission 检查。

## 固定源码映射与链接方式

源码身份：repository `https://github.com/microsoft/DiskANN.git`，branch `cpp_main`，commit `78256bbab4685e1774e78d331e081a153be26823`。复用 [Baseline source mapping](../../specs/002-cpp-main-upstream-baseline/source-mapping.md) 的 SM-015～018、SM-019、SM-033，并核对以下固定源码：

| 顺序 | 原生符号/位置 | 本模块调用方式 |
|---|---|---|
| 1 | `gen_random_slice<float>`，`src/partition.cpp:95–142` | 从已校验 Dataset 文件抽样；接着检查实际样本数 |
| 2 | `kmeans::kmeanspp_selecting_pivots`，`src/math_utils.cpp:380–456` | 保留 KMeans++；样本上限检查阻止进入其随机 pivot 备用分支 |
| 3 | `kmeans::run_lloyds`，`src/math_utils.cpp:312–354` | 传入配置的迭代上限；检查返回 residual 为有限值 |
| 4 | `diskann::save_bin<float>`，`include/utils.h` | 保存 K × dimension 原生 float32 质心，立即校验内容 |
| 5 | `shard_data_into_clusters_only_ids<float>`，`src/partition.cpp:325–407` | 对全量数据按最近质心分配，`k_base=1`；按遍历顺序写升序 GlobalNodeId |
| 6 | 框架原生格式校验和 PartitionArtifact validator | 检查非空、K、范围、长度、排序、全量且唯一覆盖、质心、payload、来源及 checksum |

该 commit 的 `partition.cpp:608–659` 没有显式实例化两个独立 sharder 模板。Linux Release 静态库实测不能直接链接 ID-only 符号，符号检查也未发现输出向量的 sharder。最终采用仓库外层的 `native_partition.cpp`：引用**未改动**的固定源码，并显式实例化 ID-only float 模板。该对象先于 `diskann_s` 链接，满足 partition.cpp 的符号，避免从静态库再提取第二份 partition 对象。CMake 继承原生编译选项、定义和 include 路径；其余原生数学函数来自 `diskann_s`。不复制/重写算法，不修改上游文件，不添加运行时 fallback。

已验证构建方式为 Linux amd64、GNU C++、CMake Unix Makefiles、MKL/Intel OpenMP。构建证据读取此 generator 的 `compile_commands.json` 和运行器 `link.txt`；其他 generator/平台尚未验证。

与 P0 的直接区别：K 由配置给定，不运行 RAM 预算推导 K 的循环；只抽取一份训练样本；硬分块取代 P0 的复制因子 2；使用独立实现身份。P0、OGP、B0、M0 的代码和参数均不改变。

## 配置和小规模执行

在安装固定 Baseline 所需依赖的 Linux 环境，从仓库根目录执行（构建目录须独立）：

```sh
cmake -S . -B build/kmeans-local -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON -DDISKANN_KMEANS_PARTITION=ON
cmake --build build/kmeans-local --target diskann_kmeans_tests -j 4
ctest --test-dir build/kmeans-local -R '^diskann_kmeans_parameters_contract$' --output-on-failure
ctest --test-dir build/kmeans-local -R '^diskann_kmeans_native_b0$' --output-on-failure
```

离线构建可用 `-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/绝对路径/已有nlohmann_json源码`。本次容器构建使用 `build/kmeans-partition-linux-20261013`，源码只读挂载，只有该构建目录可写，禁用网络。

提供 [`small.json`](../../experiments/configs/diskann-kmeans/small.json) 配置模板。使用已有 Dataset 时，填写 workspace 和合法 ArtifactRef；原生 payload 必须位于对应 workspace 的 normal 目录中。以下测试辅助命令会在**不存在的新目录**准备真实 96 × 4 float32 小数据、DatasetArtifact 及填好 checksum 的配置，不调用替代算法：

```sh
build/kmeans-local/tests/cpp/partition/diskann_kmeans_tests --prepare build/kmeans-demo
build/kmeans-local/src/partition/diskann_kmeans/kmeans_partition_runner execute \
  --request build/kmeans-demo/config.json \
  --result build/kmeans-demo/workspace/result.json
```

要切换到 K=3，修改 `config.json` 的 `partition.K` 为 3，使用新的 `run_id`（如 `kmeans-k3`），结果另存为 `workspace/result-k3.json`；无需改源码或重编译。也可在另一个新目录准备 fixture。`max_k_means_reps` 可同样通过配置修改或省略。已有 result/run 路径会拒绝重写。

本次 macOS 工作区已有构建产物可在相同 Linux 容器中直接使用。下面创建一个新的小数据演示目录（重跑时换目录名）；不会访问 SIFT 数据：

```sh
task_repo="$PWD"
docker run --rm --platform linux/amd64 --pull=never --network=none \
  --mount "type=bind,src=$task_repo,dst=/workspace,readonly" \
  --mount "type=bind,src=$task_repo/build/kmeans-partition-linux-20261013,dst=/workspace/build/kmeans-partition-linux-20261013" \
  --workdir /workspace \
  sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf \
  bash -lc 'set -eu
    build/kmeans-partition-linux-20261013/tests/cpp/partition/diskann_kmeans_tests \
      --prepare build/kmeans-partition-linux-20261013/demo-k2
    build/kmeans-partition-linux-20261013/src/partition/diskann_kmeans/kmeans_partition_runner execute \
      --request build/kmeans-partition-linux-20261013/demo-k2/config.json \
      --result build/kmeans-partition-linux-20261013/demo-k2/workspace/result.json'
```

运行器仅注册新 Partition + 固定 B0 + 固定 M0，调用现有 `PipelineExecutor`，复用兼容性、参数、产物校验、发布和 manifest 生命周期。完整流水线的 M0 是现有执行机制的闭环；本次目标证据是 Partition/B0 的结构与性质，不扩大认证。B0 固定线程数为 6，因此此入口要求 `thread_budget=6`；内存预算是记录/准入预算，并非原生分配器硬限额。入口保留 AGENTS.md 的大于 SIFT1M 人工审批闸门（无绕过开关），该闸门不是模块的算法规模限制。

## 产物、来源和资源

PartitionArtifact 包含 K 个 `partition-i-map.bin`、`centroids.bin` 和 `artifact.json`：map 的 native header 为 uint32 行数及维度 1；质心 header 为 K 和向量维度。各 map 行位置是 LocalNodeId，值是 GlobalNodeId；质心第 i 行对应 partition_id=i。不输出分块向量；B0 仍按已验证的 Dataset + map 重建向量，并产生独立 LocalGraphArtifact。

父 BuildManifest 位于 `manifests/<run_id>/stages/partition.json`，通过路径和 SHA 绑定 `stage-execution/partition.json`。后者记录实现、固定源码和原生库内容身份、运行二进制 SHA、配置时的 wrapper 源码身份（含 dirty checksum）、真实编译/链接命令、数据集 checksum、有效参数、实际 p 和训练样本数、输入输出、时间、预算、状态及失败原因。父 manifest 绑定配置摘要及发布后的 ArtifactRef。阶段自有 manifest 记录校验时 temporary 下的 candidate；现有执行器随后把它移动到 normal，其内容 checksum 不变，发布后的可解析路径以父 manifest 为准。指标/验证报告另存，不混入 Artifact。

阶段 elapsed 时间含参数校验、抽样、初始化、迭代、全量分配、质心/ID/Artifact I/O 和校验；不含 B0。RSS 为 Linux 进程全生命周期峰值，不能解读为阶段增量。原生读取仍有 64 MiB reader cache、最多 5,000,000 点的原始/float 缓冲区以及最近质心计算工作区，封装还需要覆盖位图；抽样阶段临时保存训练向量。没有改写缓冲策略，不能据此声称 SIFT10M 内存或性能已验证。

硬分块不产生跨分块副本，M0 也不创造跨分块候选或修复连通性；结构有效不代表搜索质量已认证。本次不做搜索、PQ 或磁盘布局验证。

## 后续验证边界

已完成单次 SIFT1M 原生验证：K=3、迭代上限 15、6 线程；Partition、3 份 B0 局部图和 M0 全局图通过功能及结构验证，详见 [SIFT1M 验证报告](../../experiments/reports/diskann-kmeans-sift1m-20261013-k3-01/verification.json)。该结果仅适用于已验证配置；未运行 Search、PQ、磁盘布局或 SIFT10M，未作 Recall/QPS 或性能认证。SIFT10M 仍需明确运行批准、内存/I/O/时间预算评估、有效训练样本检查、全量输出校验及独立的真实运行证据；本次没有启动或授权这些实验。
