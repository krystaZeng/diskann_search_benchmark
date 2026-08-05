#include "diskann_pipeline/adapter_policy.hpp"

#include <algorithm>
#include <set>

namespace diskann_pipeline {
namespace {

bool valid_sha256(const std::string& value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

AdapterPolicyResult reject(std::string reason) {
  return {false,
          FailureInfo{ErrorCategory::kCapabilityUnsatisfied,
                      std::move(reason), std::nullopt}};
}

}  // namespace

AdapterPolicyResult AdapterPolicyValidator::validate(
    const AdapterPolicyProposal& proposal) const {
  if (proposal.conversions.empty() ||
      proposal.accepted_format_version.empty() ||
      proposal.produced_format_version.empty()) {
    return reject("Adapter format and conversion declarations are required");
  }
  if (proposal.evidence.algorithm_semantics_changed ||
      std::find(proposal.conversions.begin(), proposal.conversions.end(),
                AdapterConversionKind::kAlgorithmSemanticChange) !=
          proposal.conversions.end()) {
    return reject("algorithm-semantic changes are not Adapter conversions");
  }
  if (std::find(proposal.conversions.begin(), proposal.conversions.end(),
                AdapterConversionKind::kIdRelabeling) !=
          proposal.conversions.end() &&
      (proposal.accepted_id_space == IdSpace::kNone ||
       proposal.produced_id_space == IdSpace::kNone ||
       proposal.accepted_id_space == proposal.produced_id_space)) {
    return reject("Local/Global ID relabeling requires explicit distinct ID spaces");
  }
  if (proposal.evidence.expected_occurrence_count == 0 ||
      proposal.evidence.trace.size() !=
          proposal.evidence.expected_occurrence_count) {
    return reject("source-to-target trace is missing or incomplete");
  }
  std::set<std::string> sources;
  std::set<std::string> targets;
  for (const auto& entry : proposal.evidence.trace) {
    if (entry.source.empty() || entry.target.empty() ||
        !sources.insert(entry.source).second ||
        !targets.insert(entry.target).second) {
      return reject("source-to-target trace is ambiguous");
    }
  }
  if (proposal.evidence.equivalence_kind == AdapterEquivalenceKind::kNone ||
      proposal.evidence.equivalence_evidence.empty()) {
    return reject("round-trip or canonical equivalence evidence is required");
  }
  if (!valid_sha256(proposal.evidence.source_checksum) ||
      !valid_sha256(proposal.evidence.output_checksum)) {
    return reject("both source and output checksums must be retained");
  }
  if (!proposal.evidence.logical_record_order_preserved ||
      !proposal.evidence.adjacency_order_preserved ||
      !proposal.evidence.adjacency_content_preserved) {
    return reject("observable record or adjacency semantics changed");
  }
  if (!proposal.evidence.replicas_distinguishable) {
    return reject("replica aggregation is not representation preserving");
  }
  if (std::find(proposal.conversions.begin(), proposal.conversions.end(),
                AdapterConversionKind::kRangeSafeIntegerWidth) !=
          proposal.conversions.end() &&
      !proposal.evidence.integer_conversion_range_safe) {
    return reject("integer-width conversion is not proven range-safe");
  }
  return {true, std::nullopt};
}

}  // namespace diskann_pipeline
