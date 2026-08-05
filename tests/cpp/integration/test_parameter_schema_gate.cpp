#include "fakes/fake_pipeline.hpp"

#include "support/test_assert.hpp"

#include <functional>
#include <string>

namespace dp = diskann_pipeline;

namespace {

struct Counts {
  std::size_t partition{0};
  std::size_t local{0};
  std::size_t merge{0};
};

dp::PipelineExecutionResult execute(
    const std::string& identity,
    const std::function<void(dp::PipelineExecutionRequest&,
                             dp::ImplementationDescriptor&,
                             dp::ImplementationDescriptor&,
                             dp::ImplementationDescriptor&)>& mutate,
    Counts* counts) {
  dp::test::FakeStageEnvironment environment(identity);
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  auto partition = dp::test::fake_partition_descriptor();
  auto local = dp::test::fake_local_graph_descriptor();
  auto merge = dp::test::fake_merge_descriptor();
  auto request = dp::test::canonical_execution_request(identity, dataset);
  mutate(request, partition, local, merge);

  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  registry.register_partition(partition, [counts] {
    ++counts->partition;
    return dp::test::make_fake_partition();
  });
  registry.register_local_build(local, [counts] {
    ++counts->local;
    return dp::test::make_fake_local_graph();
  });
  registry.register_merge(merge, [counts] {
    ++counts->merge;
    return dp::test::make_fake_merge();
  });
  auto context = environment.make_context(identity + "-executor");
  return dp::PipelineExecutor(registry, environment.workspace())
      .execute(request, context);
}

void expect_rejected_before_factory(
    const std::string& identity,
    const std::function<void(dp::PipelineExecutionRequest&,
                             dp::ImplementationDescriptor&,
                             dp::ImplementationDescriptor&,
                             dp::ImplementationDescriptor&)>& mutate) {
  Counts counts;
  const auto result = execute(identity, mutate, &counts);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(result.failure.has_value());
  DP_EXPECT_EQ(result.failure->category, dp::ErrorCategory::kSchemaInvalid);
  DP_EXPECT_EQ(counts.partition, std::size_t{0});
  DP_EXPECT_EQ(counts.local, std::size_t{0});
  DP_EXPECT_EQ(counts.merge, std::size_t{0});
  DP_EXPECT_EQ(result.invocations.partition, std::size_t{0});
  DP_EXPECT_EQ(result.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(result.invocations.merge, std::size_t{0});
}

}  // namespace

int main() {
  Counts valid_counts;
  const auto valid = execute(
      "parameter-valid", [](auto&, auto&, auto&, auto&) {}, &valid_counts);
  DP_EXPECT_EQ(valid.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(valid_counts.partition, std::size_t{1});
  DP_EXPECT_EQ(valid_counts.local, std::size_t{2});
  DP_EXPECT_EQ(valid_counts.merge, std::size_t{1});

  expect_rejected_before_factory(
      "parameter-missing-required", [](auto& request, auto&, auto&, auto&) {
        request.partition_parameters_json = "{\"partition_count\":2}";
      });
  expect_rejected_before_factory(
      "parameter-additional", [](auto& request, auto&, auto&, auto&) {
        request.local_parameters_json =
            "{\"adjacency\":[[1,2],[0,2],[1,3],[]],"
            "\"declared_max_degree\":2,\"extra\":true}";
      });
  expect_rejected_before_factory(
      "parameter-wrong-type", [](auto& request, auto&, auto&, auto&) {
        request.merge_parameters_json =
            "{\"candidate_order\":\"first_occurrence\","
            "\"declared_max_degree\":\"3\","
            "\"deduplicate\":\"exact_global_node_id\","
            "\"partition_order\":\"ascending_partition_id\","
            "\"truncate\":\"after_deduplication\"}";
      });
  expect_rejected_before_factory(
      "parameter-missing-schema", [](auto&, auto& descriptor, auto&, auto&) {
        descriptor.parameter_schema_path =
            "tests/fixtures/fake_pipeline/negative/missing-schema.json";
      });
  expect_rejected_before_factory(
      "parameter-invalid-schema", [](auto&, auto& descriptor, auto&, auto&) {
        descriptor.parameter_schema_path =
            "tests/fixtures/fake_pipeline/negative/invalid-parameter-schema.json";
      });
  return dp::test::finish();
}
