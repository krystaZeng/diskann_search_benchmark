#include "diskann_pipeline/checksum.hpp"

#include "support/test_assert.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dp = diskann_pipeline;

namespace {

constexpr const char* kDatasetChecksum =
    "2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b";
constexpr const char* kDatasetAggregate =
    "4cfb7bdb7ce50e3b9f07abe72b918a2666d35ef07211d675c708c0e1d0ecf12d";
constexpr const char* kPartitionAggregate =
    "a640037c2d07b2dcdff98cd22b82b5c4f5da7e6323fc8da2a61538c1cbbb2984";
constexpr const char* kLocal0Aggregate =
    "31d93424d2d0a826891c5d2cc6d3b5b7da6f646a9c6f878603d532ddfae0c9c1";
constexpr const char* kLocal1Aggregate =
    "ca02d15d4709d62aea8f404de57d2143be1205f6fa517b4d2d550fe2dab265f6";
constexpr const char* kGlobalAggregate =
    "443e732732f559249248d749c1fac19af9f1986fc44dbdf25f3c2ed7de59a0fe";
constexpr const char* kSift1mDatasetChecksum =
    "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e";
constexpr const char* kSift1mDatasetAggregate =
    "4935b25f2cb2af2a05c7a657c3beb9c1823f45d1cf067f1d298169bb721f377e";

dp::SourceArtifactIdentity source(dp::ArtifactType type,
                                  const std::string& format,
                                  const std::string& checksum) {
  return {type, format, checksum};
}

dp::AggregateIdentityProjection dataset_projection() {
  return {
      dp::ArtifactType::kDataset,
      "fake-dataset-json/v1",
      kDatasetChecksum,
      dp::IdSpace::kNone,
      {"dataset.vector_access/v1"},
      {{"vectors", 49, kDatasetChecksum}},
      {},
      R"({"global_node_count":6,"vector_dimension":2,"scalar_type":"float32","distance_metric":"l2"})"};
}

dp::AggregateIdentityProjection sift1m_dataset_projection() {
  return {
      dp::ArtifactType::kDataset,
      "diskann-float32-dataset/v1",
      kSift1mDatasetChecksum,
      dp::IdSpace::kNone,
      {"dataset.diskann_float32_bin/v1"},
      {{"vectors", 512000008, kSift1mDatasetChecksum}},
      {},
      R"({"global_node_count":1000000,"vector_dimension":128,"scalar_type":"float32","distance_metric":"l2"})"};
}

dp::AggregateIdentityProjection partition_projection() {
  return {
      dp::ArtifactType::kPartition,
      "partition-artifact/v1",
      kDatasetChecksum,
      dp::IdSpace::kLocal,
      {"partition.overlap/v1", "partition.complete_local_to_global_map/v1"},
      {{"partition/0/vectors", 37,
        "a61d72902e4771175bf0767a13cc9006dd129ea92439fc092d167b632d35437c"},
       {"partition/0/local_to_global_map", 29,
        "76fb4d67995b155a9755dd54977692290b7cdf416294229b4f320b81efd5ce7c"},
       {"partition/1/vectors", 37,
        "119a82674838210f5d056a5f1b784d91e11e84bf2b4113afa236a70736abd242"},
       {"partition/1/local_to_global_map", 29,
        "90923604fcdf8dcfa013e82292ecdba7d015e4b77638cbb387c9a583450f493c"}},
      {source(dp::ArtifactType::kDataset, "fake-dataset-json/v1",
              kDatasetAggregate)},
      R"({"partition_count":2,"partitions":[{"partition_id":0,"local_node_count":4,"shard_vectors_role":"partition/0/vectors","local_to_global_map_role":"partition/0/local_to_global_map"},{"partition_id":1,"local_node_count":4,"shard_vectors_role":"partition/1/vectors","local_to_global_map_role":"partition/1/local_to_global_map"}]})"};
}

dp::AggregateIdentityProjection local_projection(std::uint32_t partition_id,
                                                 const std::string& map_hash) {
  return {
      dp::ArtifactType::kLocalGraph,
      "local-graph-artifact/v1",
      kDatasetChecksum,
      dp::IdSpace::kLocal,
      {"graph.local_adjacency/v1"},
      {{"local_to_global_map", 29, map_hash},
       {"local_graph", 36,
        "e54ec8ef5014c15c861cef23ea8aa2edddd1a587b1a30614abd9b0a4defbeddd"}},
      {source(dp::ArtifactType::kPartition, "partition-artifact/v1",
              kPartitionAggregate)},
      std::string("{\"partition_id\":") + std::to_string(partition_id) +
          ",\"local_node_count\":4,\"declared_max_degree\":2,"
          "\"observed_max_degree\":2,\"source_partition_checksum\":\"" +
          kPartitionAggregate + "\"}"};
}

dp::AggregateIdentityProjection global_projection() {
  const auto partition = source(dp::ArtifactType::kPartition,
                                "partition-artifact/v1", kPartitionAggregate);
  const auto local0 = source(dp::ArtifactType::kLocalGraph,
                             "local-graph-artifact/v1", kLocal0Aggregate);
  const auto local1 = source(dp::ArtifactType::kLocalGraph,
                             "local-graph-artifact/v1", kLocal1Aggregate);
  return {
      dp::ArtifactType::kGlobalGraph,
      "global-graph-artifact/v1",
      kDatasetChecksum,
      dp::IdSpace::kGlobal,
      {"graph.global_adjacency/v1"},
      {{"global_graph", 50,
        "9cbfeade7ef24a5b7b1911da4ff7baead15b7444e338e3b0a5ee42ed780d622f"}},
      {partition, local0, local1},
      std::string("{\"global_node_count\":6,\"declared_max_degree\":3,"
                  "\"observed_max_degree\":3,\"source_partition_artifact\":{") +
          "\"artifact_type\":\"partition\",\"format_version\":"
          "\"partition-artifact/v1\",\"aggregate_checksum\":\"" +
          kPartitionAggregate +
          "\"},\"source_local_graph_artifacts\":[{\"artifact_type\":"
          "\"local_graph\",\"format_version\":\"local-graph-artifact/v1\","
          "\"aggregate_checksum\":\"" +
          kLocal0Aggregate +
          "\"},{\"artifact_type\":\"local_graph\",\"format_version\":"
          "\"local-graph-artifact/v1\",\"aggregate_checksum\":\"" +
          kLocal1Aggregate + "\"}]}"};
}

dp::AggregateIdentityProjection pq_projection() {
  return {
      dp::ArtifactType::kPQ,
      "pq-artifact/v1",
      kDatasetChecksum,
      dp::IdSpace::kNone,
      {"search.pq_in_memory/v1", "search.pqflash_loadable/v1"},
      {{"pq_pivots", 2132, std::string(64, 'a')},
       {"pq_compressed_vectors", 20, std::string(64, 'b')}},
      {source(dp::ArtifactType::kDataset, "fake-dataset-json/v1",
              kDatasetAggregate)},
      R"({"point_count":6,"dimension":2,"scalar_type":"float32","distance_metric":"l2","centroid_count":256,"pq_chunks":2,"opq":false,"compressed_code_scalar":"uint8"})"};
}

dp::AggregateIdentityProjection disk_layout_projection() {
  return {
      dp::ArtifactType::kDiskLayout,
      "disk-layout-artifact/v1",
      kDatasetChecksum,
      dp::IdSpace::kGlobal,
      {"graph.global_ids/v1", "search.disk_layout/v1",
       "search.disk_vectors_full_precision/v1", "search.entry_end_to_end/v1",
       "search.pqflash_loadable/v1"},
      {{"disk_index", 8192, std::string(64, 'c')},
       {"medoids", 12, std::string(64, 'd')}},
      {source(dp::ArtifactType::kDataset, "fake-dataset-json/v1",
              kDatasetAggregate),
       source(dp::ArtifactType::kGlobalGraph, "global-graph-artifact/v1",
              kGlobalAggregate)},
      R"({"global_node_count":6,"dimension":2,"scalar_type":"float32","distance_metric":"l2","disk_vector_encoding":"float32_full_precision","pq_disk_bytes":0,"reorder":false,"declared_max_degree":3,"medoid_count":1,"entry_policy":"end_to_end","centroid_policy":"medoid_vector_fallback","frozen_point_count":0})"};
}

}  // namespace

int main() {
  DP_EXPECT_EQ(dp::sha256_bytes(
                   R"({"vectors":[[0,0],[1,0],[2,0],[0,1],[1,1],[2,1]]})"),
               std::string(kDatasetChecksum));

  DP_EXPECT_EQ(dp::canonicalize_restricted_json(
                   R"({"z":[],"a":"é","control":"\b\t\n\f\r"})"),
               std::string(R"({"a":"é","control":"\b\t\n\f\r","z":[]})"));
  DP_EXPECT_THROW(dp::canonicalize_restricted_json(R"({"a":null})"));
  DP_EXPECT_THROW(dp::canonicalize_restricted_json(R"({"a":1.5})"));
  DP_EXPECT_THROW(dp::canonicalize_restricted_json(R"({"a":1,"a":2})"));

  DP_EXPECT_EQ(dp::aggregate_checksum(dataset_projection()),
               std::string(kDatasetAggregate));
  DP_EXPECT_EQ(
      dp::make_aggregate_projection_json(sift1m_dataset_projection()),
      std::string(
          "{\"artifact_type\":\"dataset\",\"canonicalization\":\"diskann-"
          "artifact-c14n-v1\",\"capabilities\":[\"dataset.diskann_"
          "float32_bin/v1\"],\"dataset_checksum\":\"" +
          std::string(kSift1mDatasetChecksum) +
          "\",\"format_version\":\"diskann-float32-dataset/v1\",\"id_space\":"
          "\"none\",\"payloads\":[{\"byte_length\":512000008,\"role\":"
          "\"vectors\",\"sha256\":\"" +
          std::string(kSift1mDatasetChecksum) +
          "\"}],\"schema_version\":\"1.0.0\",\"source_artifacts\":[],"
          "\"structural\":{\"distance_metric\":\"l2\",\"global_node_count\":"
          "1000000,\"scalar_type\":\"float32\",\"vector_dimension\":128}}"));
  DP_EXPECT_EQ(dp::aggregate_checksum(sift1m_dataset_projection()),
               std::string(kSift1mDatasetAggregate));
  DP_EXPECT_EQ(dp::aggregate_checksum(partition_projection()),
               std::string(kPartitionAggregate));
  DP_EXPECT_EQ(
      dp::aggregate_checksum(local_projection(
          0, "76fb4d67995b155a9755dd54977692290b7cdf416294229b4f320b81efd5ce7c")),
      std::string(kLocal0Aggregate));
  DP_EXPECT_EQ(
      dp::aggregate_checksum(local_projection(
          1, "90923604fcdf8dcfa013e82292ecdba7d015e4b77638cbb387c9a583450f493c")),
      std::string(kLocal1Aggregate));
  DP_EXPECT_EQ(dp::aggregate_checksum(global_projection()),
               std::string(kGlobalAggregate));

  const auto pq_checksum = dp::aggregate_checksum(pq_projection());
  const auto layout_checksum = dp::aggregate_checksum(disk_layout_projection());
  DP_EXPECT_EQ(pq_checksum.size(), std::size_t{64});
  DP_EXPECT_EQ(layout_checksum.size(), std::size_t{64});
  auto changed_pq = pq_projection();
  changed_pq.payloads[1].byte_length += 1;
  DP_EXPECT_TRUE(dp::aggregate_checksum(changed_pq) != pq_checksum);
  auto changed_layout = disk_layout_projection();
  changed_layout.source_artifacts[1].aggregate_checksum = std::string(64, '0');
  DP_EXPECT_TRUE(dp::aggregate_checksum(changed_layout) != layout_checksum);

  auto path_independent = dataset_projection();
  DP_EXPECT_EQ(dp::aggregate_checksum(path_independent),
               dp::aggregate_checksum(dataset_projection()));
  return dp::test::finish();
}
