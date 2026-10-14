# 分区紧凑性评估 v1

独立 Python 评估器 `partition_full_mean_squared_l2_v1`（实现版本 `1.0.0`），只读取已有产物，不调用 Partition、B0、M0、Search、PQ 或磁盘布局。依赖仓库已有 Python/jsonschema 工具，无 NumPy 等新增依赖。

输入范围是 `diskann-float32-dataset/v1` 的 float32、L2 向量，以及 `partition-artifact/v1` 的完整、无重叠、非空分块，映射编码为原生 uint32 ID map。重叠、缺失覆盖或其他编码会报告“不在当前评估器支持范围”，不据此断言违反公共 PartitionArtifact 契约。块内重复 ID、越界、身份/长度/checksum 不符等会拒绝评估。没有训练质心也可评估；存在时按其形状、内容和 capability 声明校验，但计算不用训练质心。

对分块 j 的全部 n_j 个向量，计算：

```text
μ_j = Σ x / n_j
C_j = Σ ||x − μ_j||² / n_j
C   = Σ(n_j × C_j) / Σ n_j，且 Σ n_j = N
```

平方 L2 是各维差值平方之和，不开平方、不除以维度。第一遍计算全量均值，第二遍计算关于该均值的平方距离；使用 Python float（float64）、Neumaier 补偿求和和 `math.fsum`，不使用 `E[x²] − E[x]²`。不持久化均值向量，不计算两两距离。

## 执行

在仓库根目录创建一个新请求 JSON。workspace 和 report_path 必须为绝对路径；ArtifactRef 的 metadata_path 相对于 workspace/normal。aggregate_checksum 必须使用对应产物的身份，不能填向量文件 SHA。

```json
{
  "request_version": "partition-compactness-request/v1",
  "workspace": "/absolute/path/to/existing/workspace",
  "dataset": {
    "artifact_type": "dataset",
    "format_version": "diskann-float32-dataset/v1",
    "metadata_path": "datasets/sift1m/artifact.json",
    "aggregate_checksum": "<DatasetArtifact aggregate checksum>"
  },
  "partition": {
    "artifact_type": "partition",
    "format_version": "partition-artifact/v1",
    "metadata_path": "<existing-run>/partition/artifact.json",
    "aggregate_checksum": "<PartitionArtifact aggregate checksum>"
  },
  "batch_rows": 4096,
  "report_path": "/absolute/path/to/new/reports/compactness.json"
}
```

```sh
PYTHONPATH=harness/python .venv/bin/python -B -m diskann_harness.partition_compactness \
  --request /absolute/path/to/request.json
```

`batch_rows` 必须是正整数。N、维度和 K 从输入元数据取得，没有新增固定 1M 容量上限；**执行超过 SIFT1M 的评估仍必须事先取得 AGENTS.md 所要求的明确批准**。此命令不是大规模实验批准。本次只执行已有 SIFT1M 产物的一次评估。

## 校验、报告与内存

复用现有 Dataset/Partition/ArtifactRef schema、聚合身份投影、canonicalization、SHA-256、路径约束和 `write_immutable_json`。现有 Python Baseline 数据准备检查固定绑定 SIFT1M，现有完整原生 validator 由 C++ runner 提供；本评估器不调用构图或引入原生运行器依赖，只在自身范围内补充原生矩阵/映射语义检查。公共校验器、流水线、图评估器均不改变。

每次重新核对元数据、payload 长度/checksum、数据集身份、来源、头部、映射范围/计数/声明顺序、完整唯一覆盖及有限数值。两次向量扫描均重新核对读到的内容 SHA；发布前再次核对输入（含请求）文件身份、长度、mtime/ctime 和 SHA。输入应由调用方保持不可变；这不是对外部写入者的文件锁服务。

新报告使用 [独立 schema](../../contracts/partition/compactness-v1/evaluation-report.schema.json)，包含实际评估器 `.py` 文件 SHA（并检查执行期间没有变化）、两个 ArtifactRef、Dataset 内容 SHA、参数、公式、每块及整体指标、校验依据和本次执行时间。源码 SHA 仅表示该评估器源码文件，Artifact aggregate checksum 表示契约聚合身份，Dataset 内容 SHA 表示向量文件原字节，三者不混用。执行时间涵盖输入校验及计算，不是原生分区时间或性能认证。

成功报告以已有原子、不可覆盖 JSON 写入工具发布，已有路径会拒绝。API 失败抛出异常；CLI 返回 2，并向 stderr 输出含原因的 `FAILED` JSON，无伪造的零指标或成功报告。没有写回任何输入产物、历史 BuildManifest 或历史报告。报告不得写入 workspace 的 normal/manifests/temporary/quarantine。

内存不是恒定的：节点归属表为 **4N 字节**；读取缓冲约 `4 × batch_rows × D` 字节（切换批次时可能短暂同时保留新旧缓冲）；全量均值及补偿累加器为 O(KD)，Python 对象有额外开销。映射分批扫描，不保存每块完整 ID 列表；不一次持有完整向量矩阵。SIFT1M、D=128、batch_rows=4096 时，归属表约 3.81 MiB，单个原始向量批次约 2 MiB。

指标描述块内离散程度。不同 K 下更低的数值不能单独证明方案更好，不能替代 Recall/QPS 或 M1 效果验证；分批实现不表示 SIFT10M 的资源和正确性已验证。

已有 [SIFT1M 单次评估说明与结果](../../experiments/reports/partition-compactness-sift1m-20261013-k3-01/README.md)。该目录保存实际请求和不可覆盖的机器可读报告；再次评估须另建请求及输出路径。
