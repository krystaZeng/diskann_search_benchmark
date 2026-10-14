# SIFT1M 分区紧凑性：一次已有产物评估

本次仅评估已存在的 KMeans K=3 分区，未重新分区、构建 B0、Merge 或执行 Search/PQ/磁盘布局。分区原生生成参数是 K=3、迭代上限 15、6 线程；这些不是此次 Python 评估器的执行参数。

实际命令（仓库根目录）：

```sh
PYTHONPATH=harness/python .venv/bin/python -B -m diskann_harness.partition_compactness \
  --request experiments/reports/partition-compactness-sift1m-20261013-k3-01/request.json
```

退出码 0，一次评估成功。已有报告不可覆盖，原样重跑会拒绝；后续评估须使用新的请求和报告路径。

输入 workspace：`build/kmeans-partition-sift1m-20261013-k3-01/workspace`。输入可在本机读取，实际绝对路径保存在 [请求](request.json) 和 [机器报告](compactness.json) 中：

- Dataset 元数据：`normal/datasets/sift1m/artifact.json`；payload 为同目录 `vectors.bin`，1,000,000 × 128 float32/L2，512,000,008 字节。
- Dataset 内容 SHA-256：`8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`。
- DatasetArtifact aggregate checksum：`4935b25f2cb2af2a05c7a657c3beb9c1823f45d1cf067f1d298169bb721f377e`。
- Partition 元数据：`normal/kmeans-sift1m-k3-20261013-01/partition/artifact.json`。
- PartitionArtifact aggregate checksum：`048139c8c8f2661ccbe63ff34973e7489daf53125697fc23f1a5fb4217476449`。

本次重新检查 schema、原生头部/长度、payload 与 aggregate checksum、来源、映射范围及完整唯一覆盖、有限值，并在发布前复核输入未变化。训练质心已校验，但计算不使用它。

| PartitionId | 节点数 | 紧凑性 C_j |
|---|---:|---:|
| 0 | 249,798 | 88,736.10920860329 |
| 1 | 264,593 | 88,773.36040215890 |
| 2 | 485,609 | 107,276.23088288117 |
| 整体，按节点数加权 | 1,000,000 | **97,749.21555978415** |

每块以全部向量的均值 μ_j 为中心，C_j=Σ‖x−μ_j‖²/n_j；整体为 Σ(n_j C_j)/N。不开平方、不除以维度、不对分块指标作简单平均，不持久化均值向量。

执行器：`partition_full_mean_squared_l2_v1`，版本 `1.0.0`；实际评估器源码 SHA-256 为 `448869cbd601ceafa3537c741ffdbfbb5f65e792545078a681e89684c6ba674f`，独立于 Git HEAD。环境为本机 macOS 13.2.1 x86_64、Python 3.12.12，`batch_rows=4096`，两遍计算使用 float64 Neumaier 补偿求和及 `math.fsum`。

评估函数耗时约 323.038 秒，包括输入校验、计算及发布前复核，排除 Python 导入和最后的 JSON 发布；不是原生 Partition 时间或性能认证。未采集进程峰值 RSS。归属表实际为 4,000,000 字节，单个原始向量批次约 2 MiB，另有累加器、Python 对象及切换批次开销，不宣称整体恒定内存。两遍计算之外还有校验用的文件 checksum 扫描。

针对性测试最终 59 通过，相关回归 127 通过，无最终失败或跳过；详细命令、已修复的开发阶段失败、文件保留核查见 [验证记录](verification.json)。此前小规模报告中“原 pytest 入口依赖不完整旧构建目录”的历史失败说明保持原状，本轮未修复或重跑该入口。

指标描述块内离散程度。不同 K 下较低的数值不能单独证明分区方案更好。本次结果不能替代 Recall/QPS 或 M1 效果验证；分批实现不表示已完成 SIFT10M 资源及正确性验证。本次没有运行超过 1M 的评估、构建或批量实验，也未暂存、提交或推送。新实现仍待人工 diff 审查。
