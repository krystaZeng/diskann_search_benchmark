#pragma once

#include "diskann_pipeline/implementation.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace diskann_pipeline::hnsw_merger_vamana {

inline constexpr const char* kImplementationId =
    "hnsw_merger_vamana_merge_v1";
inline constexpr const char* kImplementationVersion = "1.0.0";
inline constexpr const char* kPaperRepository =
    "https://github.com/Kimchuls/HNSWMerger.git";
inline constexpr const char* kPaperBranch = "main";
inline constexpr const char* kPaperCommit =
    "88f6b7179f651cf5f284bd4e52249a4b1323b5af";
inline constexpr const char* kPaperDoi = "10.1145/3786645";
inline constexpr const char* kPaperPdfSha256 =
    "c5293d7c92b7ab5466788ddbc69dda7dad6e680e51a0373fee0b3d1374a2c8d0";

struct M1Parameters {
  std::uint32_t r{16};
  double alpha{1.2};
  std::uint32_t max_candidate_size{750};
};

ImplementationDescriptor descriptor();
M1Parameters parse_parameters(const std::string& json_text);
std::unique_ptr<GraphMerger> make_m1_merge();

}  // namespace diskann_pipeline::hnsw_merger_vamana
