#pragma once

#include "support/contract_artifact_fixture.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace diskann_pipeline::search::test {

template <typename T>
void put_scalar(std::string& bytes, std::size_t offset, T value) {
  if (bytes.size() < offset + sizeof(T)) {
    bytes.resize(offset + sizeof(T), '\0');
  }
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

inline std::string pq_pivots_bytes(std::uint32_t dimension,
                                   std::uint32_t chunks) {
  const std::size_t offsets_offset = 8;
  const std::size_t tables_offset = offsets_offset + 4 * sizeof(std::size_t);
  const std::size_t tables_size = 8 + 256ULL * dimension * sizeof(float);
  const std::size_t centroid_offset = tables_offset + tables_size;
  const std::size_t centroid_size = 8 + dimension * sizeof(float);
  const std::size_t chunks_offset = centroid_offset + centroid_size;
  const std::size_t chunks_size = 8 + (chunks + 1ULL) * sizeof(std::uint32_t);
  const std::size_t end_offset = chunks_offset + chunks_size;

  std::string bytes(end_offset, '\0');
  put_scalar<std::uint32_t>(bytes, 0, 4);
  put_scalar<std::uint32_t>(bytes, 4, 1);
  const std::array<std::size_t, 4> offsets{
      tables_offset, centroid_offset, chunks_offset, end_offset};
  for (std::size_t index = 0; index < offsets.size(); ++index) {
    put_scalar(bytes, offsets_offset + index * sizeof(std::size_t),
               offsets[index]);
  }
  put_scalar<std::uint32_t>(bytes, tables_offset, 256);
  put_scalar<std::uint32_t>(bytes, tables_offset + 4, dimension);
  put_scalar<std::uint32_t>(bytes, centroid_offset, dimension);
  put_scalar<std::uint32_t>(bytes, centroid_offset + 4, 1);
  put_scalar<std::uint32_t>(bytes, chunks_offset, chunks + 1);
  put_scalar<std::uint32_t>(bytes, chunks_offset + 4, 1);
  for (std::uint32_t index = 0; index <= chunks; ++index) {
    put_scalar(bytes, chunks_offset + 8 + index * sizeof(std::uint32_t),
               index == chunks ? dimension : index);
  }
  return bytes;
}

inline std::string pq_codes_bytes(std::uint32_t points,
                                  std::uint32_t chunks) {
  std::string bytes(8ULL + static_cast<std::size_t>(points) * chunks, '\0');
  put_scalar(bytes, 0, points);
  put_scalar(bytes, 4, chunks);
  return bytes;
}

inline std::string medoids_bytes() {
  std::string bytes(12, '\0');
  put_scalar<std::uint32_t>(bytes, 0, 1);
  put_scalar<std::uint32_t>(bytes, 4, 1);
  put_scalar<std::uint32_t>(bytes, 8, 0);
  return bytes;
}

inline std::string disk_layout_bytes(std::uint32_t points = 6,
                                     std::uint32_t dimension = 2,
                                     std::uint32_t declared_degree = 3) {
  constexpr std::uint64_t kSector = 4096;
  const std::uint64_t record_bytes =
      static_cast<std::uint64_t>(dimension) * sizeof(float) +
      (static_cast<std::uint64_t>(declared_degree) + 1) * sizeof(std::uint32_t);
  const std::uint64_t nodes_per_sector = kSector / record_bytes;
  const std::uint64_t data_sectors =
      (static_cast<std::uint64_t>(points) + nodes_per_sector - 1) /
      nodes_per_sector;
  std::string bytes((data_sectors + 1) * kSector, '\0');
  put_scalar<std::uint32_t>(bytes, 0, 9);
  put_scalar<std::uint32_t>(bytes, 4, 1);
  const std::array<std::uint64_t, 9> metadata{
      points, dimension, 0, record_bytes, nodes_per_sector,
      0,      0,         0, bytes.size()};
  for (std::size_t index = 0; index < metadata.size(); ++index) {
    put_scalar(bytes, 8 + index * sizeof(std::uint64_t), metadata[index]);
  }
  const auto observed_degree =
      std::min<std::uint32_t>(declared_degree, points - 1);
  for (std::uint32_t node = 0; node < points; ++node) {
    const std::size_t record =
        kSector + (node / nodes_per_sector) * kSector +
        (node % nodes_per_sector) * record_bytes;
    put_scalar<std::uint32_t>(bytes, record + dimension * sizeof(float),
                              observed_degree);
    for (std::uint32_t edge = 0; edge < observed_degree; ++edge) {
      const auto neighbor = (node + edge + 1) % points;
      put_scalar(bytes,
                 record + dimension * sizeof(float) + sizeof(std::uint32_t) +
                     edge * sizeof(std::uint32_t),
                 neighbor);
    }
  }
  return bytes;
}

class SearchArtifactFixture {
 public:
  SearchArtifactFixture() { materialize(); }

  diskann_pipeline::test::ContractArtifactFixture base;
  ArtifactRef pq{"pq/artifact.json", ArtifactType::kPQ, "pq-artifact/v1", {}};
  ArtifactRef layout{"layout/artifact.json", ArtifactType::kDiskLayout,
                     "disk-layout-artifact/v1", {}};

 private:
  void write(ArtifactRef& reference, diskann_pipeline::test::Json metadata,
             const std::vector<std::pair<std::string, std::string>>& payloads) {
    const auto path =
        diskann_pipeline::test::metadata_path(base.workspace, reference);
    for (const auto& payload : payloads) {
      diskann_pipeline::test::overwrite_file(path.parent_path() / payload.first,
                                             payload.second);
    }
    metadata["aggregate_checksum"] =
        diskann_pipeline::test::recompute_aggregate(metadata);
    reference.aggregate_checksum =
        metadata.at("aggregate_checksum").get<std::string>();
    diskann_pipeline::test::overwrite_file(path, metadata.dump());
  }

  void materialize() {
    const auto pivots = pq_pivots_bytes(2, 2);
    const auto codes = pq_codes_bytes(6, 2);
    write(
        pq,
        {{"schema_version", "1.0.0"},
         {"canonicalization", "diskann-artifact-c14n-v1"},
         {"artifact_type", "pq"},
         {"format_version", "pq-artifact/v1"},
         {"dataset", diskann_pipeline::test::dataset_identity()},
         {"id_space", "none"},
         {"structural",
          {{"point_count", 6},
           {"dimension", 2},
           {"scalar_type", "float32"},
           {"distance_metric", "l2"},
           {"centroid_count", 256},
           {"pq_chunks", 2},
           {"opq", false},
           {"compressed_code_scalar", "uint8"}}},
         {"capabilities",
          diskann_pipeline::test::Json::array(
              {"search.pq_in_memory/v1", "search.pqflash_loadable/v1"})},
         {"source_artifacts",
          diskann_pipeline::test::Json::array({diskann_pipeline::test::source(
              ArtifactType::kDataset, "fake-dataset-json/v1",
              diskann_pipeline::test::kContractDatasetAggregateChecksum)})},
         {"payloads",
          diskann_pipeline::test::Json::array(
              {diskann_pipeline::test::payload(
                   "pq_pivots", "pivots.bin",
                   "application/vnd.diskann.pq-pivots+binary;version=1",
                   pivots.size(), sha256_bytes(pivots).c_str()),
               diskann_pipeline::test::payload(
                   "pq_compressed_vectors", "codes.bin",
                   "application/vnd.diskann.pq-codes+binary;version=1",
                   codes.size(), sha256_bytes(codes).c_str())})},
         {"aggregate_checksum", std::string(64, '0')}},
        {{"pivots.bin", pivots}, {"codes.bin", codes}});

    const auto medoids = medoids_bytes();
    const auto global_metadata = diskann_pipeline::test::metadata_path(
        base.workspace, base.global);
    diskann_pipeline::test::overwrite_file(global_metadata.parent_path() /
                                               "medoids.bin",
                                           medoids);
    diskann_pipeline::test::mutate_metadata(
        base.workspace, base.global, [&](auto& metadata) {
          metadata["capabilities"].push_back("graph.medoids/v1");
          metadata["payloads"].push_back(diskann_pipeline::test::payload(
              "medoids", "medoids.bin",
              "application/vnd.diskann.uint32-matrix+binary;version=1",
              medoids.size(), sha256_bytes(medoids).c_str()));
        });

    const auto disk = disk_layout_bytes();
    write(
        layout,
        {{"schema_version", "1.0.0"},
         {"canonicalization", "diskann-artifact-c14n-v1"},
         {"artifact_type", "disk_layout"},
         {"format_version", "disk-layout-artifact/v1"},
         {"dataset", diskann_pipeline::test::dataset_identity()},
         {"id_space", "global"},
         {"structural",
          {{"global_node_count", 6},
           {"dimension", 2},
           {"scalar_type", "float32"},
           {"distance_metric", "l2"},
           {"disk_vector_encoding", "float32_full_precision"},
           {"pq_disk_bytes", 0},
           {"reorder", false},
           {"declared_max_degree", 3},
           {"medoid_count", 1},
           {"entry_policy", "end_to_end"},
           {"centroid_policy", "medoid_vector_fallback"},
           {"frozen_point_count", 0}}},
         {"capabilities",
          diskann_pipeline::test::Json::array(
              {"graph.global_ids/v1", "search.disk_layout/v1",
               "search.disk_vectors_full_precision/v1",
               "search.entry_end_to_end/v1",
               "search.pqflash_loadable/v1"})},
         {"source_artifacts",
          diskann_pipeline::test::Json::array(
              {diskann_pipeline::test::source(
                   ArtifactType::kDataset, "fake-dataset-json/v1",
                   diskann_pipeline::test::kContractDatasetAggregateChecksum),
               diskann_pipeline::test::source(
                   ArtifactType::kGlobalGraph, "global-graph-artifact/v1",
                   base.global.aggregate_checksum.c_str())})},
         {"payloads",
          diskann_pipeline::test::Json::array(
              {diskann_pipeline::test::payload(
                   "disk_index", "disk.index",
                   "application/vnd.diskann.pqflash-disk-index+binary;version=1",
                   disk.size(), sha256_bytes(disk).c_str()),
               diskann_pipeline::test::payload(
                   "medoids", "medoids.bin",
                   "application/vnd.diskann.uint32-matrix+binary;version=1",
                   medoids.size(), sha256_bytes(medoids).c_str())})},
         {"aggregate_checksum", std::string(64, '0')}},
        {{"disk.index", disk}, {"medoids.bin", medoids}});
  }
};

}  // namespace diskann_pipeline::search::test
