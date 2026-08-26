#include "search/diskann/fixed_descriptors.hpp"

namespace diskann_pipeline::search {

const SearchAssetDescriptor& fixed_pq_descriptor() {
  static const SearchAssetDescriptor descriptor{
      "cpp_main_post_graph_pq_v1",
      "1.0.0",
      "upstream_faithful",
      {{{ArtifactType::kDataset, {"diskann-float32-dataset/v1"},
         IdSpace::kNone}},
       {"dataset.diskann_float32_bin/v1"},
       {"workspace/v1", "memory_budget/v1", "cancellation_token/v1"}},
      {ArtifactType::kPQ,
       "pq-artifact/v1",
       IdSpace::kNone,
       {"search.pq_in_memory/v1", "search.pqflash_loadable/v1"}}};
  return descriptor;
}

const SearchAssetDescriptor& fixed_disk_layout_descriptor() {
  static const SearchAssetDescriptor descriptor{
      "cpp_main_post_graph_disk_layout_v1",
      "1.0.0",
      "upstream_faithful",
      {{{ArtifactType::kDataset, {"diskann-float32-dataset/v1"},
         IdSpace::kNone},
        {ArtifactType::kGlobalGraph,
         {"global-graph-artifact/v1", "global-graph-artifact/v2"},
         IdSpace::kGlobal}},
       {"dataset.diskann_float32_bin/v1", "graph.global_adjacency/v1",
        "graph.medoids/v1"},
       {"workspace/v1", "memory_budget/v1", "cancellation_token/v1"}},
      {ArtifactType::kDiskLayout,
       "disk-layout-artifact/v1",
       IdSpace::kGlobal,
       {"graph.global_ids/v1", "search.disk_layout/v1",
        "search.disk_vectors_full_precision/v1",
        "search.entry_end_to_end/v1", "search.pqflash_loadable/v1"}}};
  return descriptor;
}

const SearchImplementationIdentity& fixed_oracle_identity() {
  static const SearchImplementationIdentity identity{
      "cpp_main_search_disk_index_oracle_v1", "1.0.0",
      "upstream_faithful"};
  return identity;
}

const SearchImplementationIdentity& fixed_instrumented_search_identity() {
  static const SearchImplementationIdentity identity{
      "cpp_main_pqflash_search_instrumented_v1", "1.0.0", "instrumented"};
  return identity;
}

const SearchImplementationIdentity& fixed_evaluator_identity() {
  static const SearchImplementationIdentity identity{
      "cpp_main_search_metrics_evaluator_v1", "1.0.0", "instrumented"};
  return identity;
}

}  // namespace diskann_pipeline::search
