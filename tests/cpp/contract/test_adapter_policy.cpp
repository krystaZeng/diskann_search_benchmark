#include "diskann_pipeline/adapter_policy.hpp"

#include "support/test_assert.hpp"

namespace dp = diskann_pipeline;

namespace {

dp::AdapterPolicyProposal valid(dp::AdapterConversionKind kind) {
  return {{kind},
          "source-format/v1",
          "target-format/v1",
          dp::IdSpace::kLocal,
          kind == dp::AdapterConversionKind::kIdRelabeling
              ? dp::IdSpace::kGlobal
              : dp::IdSpace::kLocal,
          {{{"source/0", "target/0"}, {"source/1", "target/1"}},
           2,
           dp::AdapterEquivalenceKind::kCanonicalLogicalEquivalence,
           "logical-digest-evidence",
           std::string(64, '1'),
           std::string(64, '2'),
           true,
           true,
           true,
           true,
           true,
           false}};
}

void expect_rejected(dp::AdapterPolicyProposal proposal) {
  DP_EXPECT_TRUE(!dp::AdapterPolicyValidator{}.validate(proposal).accepted);
}

}  // namespace

int main() {
  for (const auto kind :
       {dp::AdapterConversionKind::kFormatLayoutEndianness,
        dp::AdapterConversionKind::kRangeSafeIntegerWidth,
        dp::AdapterConversionKind::kIdRelabeling,
        dp::AdapterConversionKind::kPhysicalRecordRelocation}) {
    DP_EXPECT_TRUE(dp::AdapterPolicyValidator{}.validate(valid(kind)).accepted);
  }
  auto round_trip = valid(dp::AdapterConversionKind::kFormatLayoutEndianness);
  round_trip.evidence.equivalence_kind =
      dp::AdapterEquivalenceKind::kRoundTrip;
  DP_EXPECT_TRUE(dp::AdapterPolicyValidator{}.validate(round_trip).accepted);

  auto missing_trace = valid(dp::AdapterConversionKind::kIdRelabeling);
  missing_trace.evidence.trace.clear();
  expect_rejected(missing_trace);
  auto incomplete = valid(dp::AdapterConversionKind::kIdRelabeling);
  incomplete.evidence.expected_occurrence_count = 3;
  expect_rejected(incomplete);
  auto no_equivalence = valid(dp::AdapterConversionKind::kIdRelabeling);
  no_equivalence.evidence.equivalence_kind = dp::AdapterEquivalenceKind::kNone;
  no_equivalence.evidence.equivalence_evidence.clear();
  expect_rejected(no_equivalence);
  auto record_order = valid(dp::AdapterConversionKind::kPhysicalRecordRelocation);
  record_order.evidence.logical_record_order_preserved = false;
  expect_rejected(record_order);
  auto adjacency_order = valid(dp::AdapterConversionKind::kPhysicalRecordRelocation);
  adjacency_order.evidence.adjacency_order_preserved = false;
  expect_rejected(adjacency_order);
  auto adjacency_content = valid(dp::AdapterConversionKind::kFormatLayoutEndianness);
  adjacency_content.evidence.adjacency_content_preserved = false;
  expect_rejected(adjacency_content);
  auto output_only = valid(dp::AdapterConversionKind::kFormatLayoutEndianness);
  output_only.evidence.source_checksum.clear();
  expect_rejected(output_only);
  auto overflow = valid(dp::AdapterConversionKind::kRangeSafeIntegerWidth);
  overflow.evidence.integer_conversion_range_safe = false;
  expect_rejected(overflow);
  auto ambiguous = valid(dp::AdapterConversionKind::kIdRelabeling);
  ambiguous.evidence.trace[1].source = "source/0";
  expect_rejected(ambiguous);
  auto replicas = valid(dp::AdapterConversionKind::kIdRelabeling);
  replicas.evidence.replicas_distinguishable = false;
  expect_rejected(replicas);
  auto semantic = valid(dp::AdapterConversionKind::kFormatLayoutEndianness);
  semantic.evidence.algorithm_semantics_changed = true;
  expect_rejected(semantic);
  auto semantic_kind = valid(dp::AdapterConversionKind::kAlgorithmSemanticChange);
  expect_rejected(semantic_kind);
  auto missing_format = valid(dp::AdapterConversionKind::kFormatLayoutEndianness);
  missing_format.accepted_format_version.clear();
  expect_rejected(missing_format);
  auto ambiguous_id_space = valid(dp::AdapterConversionKind::kIdRelabeling);
  ambiguous_id_space.produced_id_space = dp::IdSpace::kLocal;
  expect_rejected(ambiguous_id_space);

  return dp::test::finish();
}
