#pragma once

#include "diskann_pipeline/identifiers.hpp"
#include "diskann_pipeline/provenance.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace diskann_pipeline {

enum class AdapterConversionKind {
  kFormatLayoutEndianness,
  kRangeSafeIntegerWidth,
  kIdRelabeling,
  kPhysicalRecordRelocation,
  kAlgorithmSemanticChange,
};

enum class AdapterEquivalenceKind {
  kNone,
  kRoundTrip,
  kCanonicalLogicalEquivalence,
};

struct AdapterTraceEntry {
  std::string source;
  std::string target;
};

struct AdapterPolicyEvidence {
  std::vector<AdapterTraceEntry> trace;
  std::size_t expected_occurrence_count{0};
  AdapterEquivalenceKind equivalence_kind{AdapterEquivalenceKind::kNone};
  std::string equivalence_evidence;
  std::string source_checksum;
  std::string output_checksum;
  bool logical_record_order_preserved{false};
  bool adjacency_order_preserved{false};
  bool adjacency_content_preserved{false};
  bool integer_conversion_range_safe{false};
  bool replicas_distinguishable{false};
  bool algorithm_semantics_changed{false};
};

struct AdapterPolicyProposal {
  std::vector<AdapterConversionKind> conversions;
  std::string accepted_format_version;
  std::string produced_format_version;
  IdSpace accepted_id_space{IdSpace::kNone};
  IdSpace produced_id_space{IdSpace::kNone};
  AdapterPolicyEvidence evidence;
};

struct AdapterPolicyResult {
  bool accepted{false};
  std::optional<FailureInfo> failure;
};

class AdapterPolicyValidator {
 public:
  AdapterPolicyResult validate(const AdapterPolicyProposal& proposal) const;
};

}  // namespace diskann_pipeline
