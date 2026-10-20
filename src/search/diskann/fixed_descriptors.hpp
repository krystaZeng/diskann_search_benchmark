#pragma once

#include "diskann_pipeline/implementation.hpp"

#include <string>

namespace diskann_pipeline::search {

struct SearchAssetDescriptor {
  std::string implementation_id;
  std::string implementation_version;
  std::string reproduction_mode;
  ContractRequirement requires;
  ContractProvide provides;
};

const SearchAssetDescriptor& fixed_pq_descriptor();
const SearchAssetDescriptor& pq_descriptor(const std::string& version);
const SearchAssetDescriptor& fixed_disk_layout_descriptor();

struct SearchImplementationIdentity {
  std::string implementation_id;
  std::string implementation_version;
  std::string reproduction_mode;
};

const SearchImplementationIdentity& fixed_oracle_identity();
const SearchImplementationIdentity& fixed_instrumented_search_identity();
const SearchImplementationIdentity& fixed_evaluator_identity();

}  // namespace diskann_pipeline::search
