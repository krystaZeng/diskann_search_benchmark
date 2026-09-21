#include "papers/hnsw_merger/vamana/stage_binding.hpp"
#include "papers/hnsw_merger/vamana/runner_distance.hpp"

#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/validation.hpp"
#include "diskann_pipeline/pipeline.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <cmath>
#include <iostream>

namespace dp = diskann_pipeline;
namespace m1 = diskann_pipeline::hnsw_merger_vamana;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;

namespace {

constexpr auto kParameters =
    R"({"R":16,"alpha":1.2,"max_candidate_size":750,"metric":"l2","use_filters":false,"multi_shard_strategy":"large_first_pairwise_v1","lambda_policy":"paper_adaptive_log_v1"})";

class ObservedVectors final : public dp::VectorAccessor {
 public:
  explicit ObservedVectors(const dp::VectorAccessor& source, bool known = true)
      : source_(source), known_(known) {}
  const dp::ValidatedArtifact* bound_dataset() const noexcept override {
    return known_ ? source_.bound_dataset() : nullptr;
  }
  std::size_t dimension() const noexcept override { return source_.dimension(); }
  void read(dp::GlobalNodeId id, std::vector<float>& row) const override {
    ++reads;
    source_.read(id, row);
  }
  mutable std::size_t reads{0};
 private:
  const dp::VectorAccessor& source_;
  bool known_;
};

class ObservedDistance final : public dp::DistanceFunction {
 public:
  explicit ObservedDistance(bool euclidean, bool known = true)
      : euclidean_(euclidean), known_(known) {}
  dp::DistanceSemantics semantics() const noexcept override {
    return !known_ ? dp::DistanceSemantics::kUnknown
                  : euclidean_ ? dp::DistanceSemantics::kEuclideanL2
                               : dp::DistanceSemantics::kSquaredL2;
  }
  double evaluate(const std::vector<float>& a,
                  const std::vector<float>& b) const override {
    ++calls;
    const auto squared = squared_.evaluate(a, b);
    return euclidean_ ? std::sqrt(squared) : squared;
  }
  mutable std::size_t calls{0};
 private:
  bool euclidean_;
  bool known_;
  fixture::TestDistance squared_;
};

dp::ArtifactRef other_dataset(const dp::WorkspaceRef& workspace,
                             const fixture::NativeDatasetFixture& original) {
  auto metadata = fixture::read_json(workspace.normal_root /
                                     original.reference.metadata_path);
  const auto payload = workspace.normal_root / "other-dataset/vectors.bin";
  std::vector<float> values;
  for (unsigned i = 0; i < 12; ++i)
    for (unsigned j = 0; j < 4; ++j)
      values.push_back(static_cast<float>((i * i + 3 * j) % 17));
  fixture::write_bytes(payload, fixture::float_matrix(12, 4, values));
  // Even dataset_id is identical: the payload identity, not a name or shape,
  // must determine whether the service belongs to the requested Dataset.
  metadata["dataset"]["dataset_checksum"] = dp::sha256_file(payload);
  metadata["payloads"] = nlohmann::json::array({fixture::payload(
      "vectors", "vectors.bin", native::kFloat32DatasetMediaType, payload)});
  auto reference = original.reference;
  reference.metadata_path = "other-dataset/artifact.json";
  reference.aggregate_checksum = fixture::aggregate_for(metadata);
  metadata["aggregate_checksum"] = reference.aggregate_checksum;
  fixture::write_text(workspace.normal_root / reference.metadata_path,
                      metadata.dump());
  return reference;
}

std::vector<std::vector<std::uint32_t>> ring(std::size_t count) {
  std::vector<std::vector<std::uint32_t>> result(count);
  for (std::size_t index = 0; index < count; ++index) {
    result[index] = {static_cast<std::uint32_t>((index + 1) % count)};
  }
  return result;
}

class CancelOnFirstRead final : public dp::VectorAccessor {
 public:
  CancelOnFirstRead(const dp::VectorAccessor& source,
                    fixture::TestCancellation& cancellation)
      : source_(source), cancellation_(cancellation) {}

  std::size_t dimension() const noexcept override {
    return source_.dimension();
  }

  const dp::ValidatedArtifact* bound_dataset() const noexcept override {
    return source_.bound_dataset();
  }

  void read(dp::GlobalNodeId id,
            std::vector<float>& destination) const override {
    source_.read(id, destination);
    ++reads_;
    if (reads_ == 1) cancellation_.cancelled = true;
  }

  std::size_t reads() const noexcept { return reads_; }

 private:
  const dp::VectorAccessor& source_;
  fixture::TestCancellation& cancellation_;
  mutable std::size_t reads_{0};
};

class CountedMerge final : public dp::GraphMerger {
 public:
  explicit CountedMerge(std::size_t& calls) : calls_(calls) {}
  dp::StageResult merge(const dp::ArtifactRef& partition,
                       const std::vector<dp::ArtifactRef>& locals,
                       const std::filesystem::path& parameters,
                       dp::StageContext& context) override {
    ++calls_;
    return m1::make_m1_merge()->merge(partition, locals, parameters, context);
  }
 private:
  std::size_t& calls_;
};

}  // namespace

int main() {
  const m1::runner_detail::L2Distance runner_distance;
  DP_EXPECT_EQ(runner_distance.semantics(), dp::DistanceSemantics::kSquaredL2);
  DP_EXPECT_EQ(runner_distance.evaluate({0, 0}, {3, 4}), 25.0);
  DP_EXPECT_THROW(runner_distance.evaluate({0}, {3, 4}));
  fixture::TemporaryDirectory temporary("hnsw-merger-native-output");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  fixture::NativeDatasetFixture dataset(workspace, 12, 4);
  const std::vector<std::vector<dp::GlobalNodeId>> maps{
      {0, 1, 2, 3, 4, 5}, {4, 5, 6, 7, 8, 9}, {8, 9, 10, 11}};
  const auto partition =
      fixture::materialize_partition(workspace, dataset, maps);
  std::vector<dp::ArtifactRef> locals;
  for (std::size_t id = 0; id < maps.size(); ++id) {
    locals.push_back(fixture::materialize_local(
        workspace, dataset, partition, id, maps[id], ring(maps[id].size())));
  }

  dp::Validator validator(workspace);
  const auto valid_dataset = validator.validate(dataset.reference);
  DP_EXPECT_TRUE(valid_dataset.valid());
  if (!valid_dataset.valid()) return dp::test::finish();
  const auto valid_partition =
      validator.validate(partition, {*valid_dataset.validated_artifact});
  DP_EXPECT_TRUE(valid_partition.valid());
  if (!valid_partition.valid()) return dp::test::finish();
  std::vector<dp::ValidatedArtifact> valid_sources{*valid_partition.validated_artifact};
  for (const auto& local : locals) {
    const auto valid = validator.validate(local, {*valid_partition.validated_artifact});
    DP_EXPECT_TRUE(valid.valid());
    if (!valid.valid()) return dp::test::finish();
    valid_sources.push_back(*valid.validated_artifact);
  }

  native::CppMainVectorAccessor vectors(workspace, dataset.reference);
  const auto other = other_dataset(workspace, dataset);
  const auto other_validation = validator.validate(other);
  DP_EXPECT_TRUE(other_validation.valid());
  if (!other_validation.valid()) return dp::test::finish();
  DP_EXPECT_TRUE(other_validation.validated_artifact->dataset().dataset_checksum !=
                 valid_dataset.validated_artifact->dataset().dataset_checksum);
  native::CppMainVectorAccessor wrong_vectors(workspace, other);
  for (const std::string label : {"wrong-dataset", "euclidean", "unknown-binding",
                                 "unknown-distance", "generic-correct"}) {
    const bool success = label == "generic-correct";
    const bool wrong_dataset = label == "wrong-dataset";
    ObservedVectors observed(wrong_dataset ? wrong_vectors : vectors,
                             label != "unknown-binding");
    ObservedDistance distance(label == "euclidean", label != "unknown-distance");
    fixture::TestContext rejected_environment(workspace, "m1-service-" + label);
    rejected_environment.context.vector_accessor = &observed;
    rejected_environment.context.distance_function = &distance;
    const auto parameters = dp::resolve_confined_path(
        workspace.manifest_root, "m1-service-" + label + "/parameters/merge.json");
    dp::write_immutable_text(parameters, kParameters);
    const auto rejected = m1::make_m1_merge()->merge(
        partition, locals, parameters, rejected_environment.context);
    std::cout << "M1_SERVICE " << label << " stage=1 status="
              << dp::to_string(rejected.status) << " reads=" << observed.reads
              << " distance_calls=" << distance.calls << '\n';
    DP_EXPECT_EQ(rejected.status, success ? dp::StageStatus::kSucceeded
                                          : dp::StageStatus::kFailed);
    DP_EXPECT_EQ(rejected.output_candidate.has_value(), success);
    const auto failure_manifest = dp::parse_build_manifest(
        fixture::read_json(rejected.build_manifest_path).dump());
    if (!success) {
      DP_EXPECT_EQ(observed.reads, std::size_t{0});
      DP_EXPECT_EQ(distance.calls, std::size_t{0});
      DP_EXPECT_TRUE(failure_manifest.output_artifacts.empty());
      DP_EXPECT_TRUE(failure_manifest.failure.has_value());
      if (failure_manifest.failure) {
        DP_EXPECT_EQ(failure_manifest.failure->category,
                     wrong_dataset ? dp::ErrorCategory::kSourceMismatch
                                   : dp::ErrorCategory::kCapabilityUnsatisfied);
        DP_EXPECT_TRUE(failure_manifest.failure->reason.find("runtime service") !=
                       std::string::npos);
      }
    } else {
      DP_EXPECT_TRUE(observed.reads > 0);
      DP_EXPECT_TRUE(distance.calls > 0);
      if (rejected.output_candidate) {
        auto candidate_workspace = workspace;
        candidate_workspace.normal_root =
            rejected_environment.context.temporary_storage.stage_root;
        DP_EXPECT_TRUE(dp::Validator(candidate_workspace)
                           .validate(*rejected.output_candidate, valid_sources).valid());
      }
    }

    // The same real M1 consumer through Executor must reject before creating
    // its factory, independently of the direct stage's defensive check above.
    observed.reads = 0;
    distance.calls = 0;
    std::size_t factories = 0, stages = 0;
    dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
    registry.register_merge(m1::descriptor(), [&] {
      ++factories;
      return std::make_unique<CountedMerge>(stages);
    });
    dp::PipelineExecutionRequest request;
    request.run_id = "m1-executor-" + label;
    request.dataset = dataset.reference;
    request.merge_only_inputs = dp::MergeOnlyInputSet{partition, locals};
    request.merge_implementation_id = m1::kImplementationId;
    request.merge_parameters_json = kParameters;
    request.source = {"https://example.invalid/services.git", "test",
                      std::string(40, '1'), false, std::nullopt};
    request.toolchain = {"Linux", "/usr/bin/c++", "11.4.0", "3.22.1",
                         "Unix Makefiles", "Release", {"TEST=ON"}, {}, {}};
    request.started_at = "2026-09-21T00:00:00Z";
    request.ended_at = "2026-09-21T00:00:01Z";
    const auto executed = dp::PipelineExecutor(registry, workspace)
                              .execute(request, rejected_environment.context);
    DP_EXPECT_EQ(executed.status, success ? dp::StageStatus::kSucceeded
                                          : dp::StageStatus::kFailed);
    DP_EXPECT_EQ(factories, success ? std::size_t{1} : std::size_t{0});
    DP_EXPECT_EQ(stages, factories);
    DP_EXPECT_EQ(executed.invocations.partition, std::size_t{0});
    DP_EXPECT_EQ(executed.invocations.local_build, std::size_t{0});
    DP_EXPECT_EQ(executed.invocations.merge, stages);
    DP_EXPECT_EQ(executed.artifact.has_value(), success);
    DP_EXPECT_TRUE(!executed.quarantine_id.has_value());
    DP_EXPECT_TRUE(executed.validation_evidence_paths.size() >= 5);
    for (const auto& evidence : executed.validation_evidence_paths) {
      const auto document = fixture::read_json(evidence);
      DP_EXPECT_EQ(document.at("result"), "VALID");
    }
    if (!success) {
      DP_EXPECT_EQ(observed.reads, std::size_t{0});
      DP_EXPECT_EQ(distance.calls, std::size_t{0});
      DP_EXPECT_TRUE(executed.build_manifest_paths.empty());
      DP_EXPECT_TRUE(executed.failure.has_value());
      if (executed.failure) DP_EXPECT_EQ(executed.failure->category,
          wrong_dataset ? dp::ErrorCategory::kSourceMismatch
                        : dp::ErrorCategory::kCapabilityUnsatisfied);
    }
    std::cout << "M1_EXECUTOR " << label << " factories=" << factories
              << " stages=" << stages << " executor=" << executed.invocations.merge
              << " reads=" << observed.reads << " distance_calls=" << distance.calls
              << " evidence=VALID quarantine=0\n";
  }
  fixture::TestContext environment(workspace, "hnsw-merger-m1");
  environment.context.vector_accessor = &vectors;
  const auto parameter_path = dp::resolve_confined_path(
      workspace.manifest_root, "native-output/parameters/merge.json");
  dp::write_immutable_text(parameter_path, kParameters);
  const auto stage = m1::make_m1_merge()->merge(
      partition, locals, parameter_path, environment.context);
  DP_EXPECT_EQ(stage.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(stage.output_candidate.has_value());
  DP_EXPECT_TRUE(!stage.build_manifest_path.empty());
  if (stage.status != dp::StageStatus::kSucceeded ||
      !stage.output_candidate) {
    return dp::test::finish();
  }
  const auto execution_manifest = dp::parse_build_manifest(
      fixture::read_json(stage.build_manifest_path).dump());
  DP_EXPECT_EQ(execution_manifest.status, dp::StageStatus::kSucceeded);
  const auto provenance = nlohmann::json::parse(
      execution_manifest.parameters_json);
  DP_EXPECT_EQ(provenance.at("paper_source_identity").at("commit"),
               std::string(m1::kPaperCommit));
  DP_EXPECT_EQ(provenance.at("pairwise_merge_decisions").size(),
               std::size_t{2});

  auto candidate_workspace = workspace;
  candidate_workspace.normal_root = environment.context.temporary_storage.stage_root;
  const auto valid_global = dp::Validator(candidate_workspace).validate(
      *stage.output_candidate, valid_sources);
  DP_EXPECT_TRUE(valid_global.valid());
  if (valid_global.valid()) {
    DP_EXPECT_EQ(valid_global.validated_artifact->dataset().global_node_count,
                 dp::NodeCount{12});
    DP_EXPECT_TRUE(valid_global.validated_artifact->has_capability(
        "graph.medoids/v1"));
  }


  fixture::TestContext cancelled_environment(workspace,
                                             "hnsw-merger-m1-cancel");
  CancelOnFirstRead cancelling_vectors(
      vectors, cancelled_environment.cancellation);
  cancelled_environment.context.vector_accessor = &cancelling_vectors;
  const auto cancelled_parameter_path = dp::resolve_confined_path(
      workspace.manifest_root, "native-output-cancel/parameters/merge.json");
  dp::write_immutable_text(cancelled_parameter_path, kParameters);
  const auto cancelled = m1::make_m1_merge()->merge(
      partition, locals, cancelled_parameter_path,
      cancelled_environment.context);
  DP_EXPECT_EQ(cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_TRUE(!cancelled.output_candidate.has_value());
  DP_EXPECT_EQ(cancelling_vectors.reads(), std::size_t{1});

  fixture::TestContext budget_environment(workspace,
                                          "hnsw-merger-m1-budget");
  CancelOnFirstRead budget_vectors(vectors, budget_environment.cancellation);
  budget_environment.context.vector_accessor = &budget_vectors;
  budget_environment.context.memory_budget_bytes = 1;
  const auto budget_parameter_path = dp::resolve_confined_path(
      workspace.manifest_root, "native-output-budget/parameters/merge.json");
  dp::write_immutable_text(budget_parameter_path, kParameters);
  const auto budget_rejected = m1::make_m1_merge()->merge(
      partition, locals, budget_parameter_path, budget_environment.context);
  DP_EXPECT_EQ(budget_rejected.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!budget_rejected.output_candidate.has_value());
  DP_EXPECT_EQ(budget_vectors.reads(), std::size_t{0});
  const auto budget_manifest = dp::parse_build_manifest(
      fixture::read_json(budget_rejected.build_manifest_path).dump());
  DP_EXPECT_TRUE(budget_manifest.failure.has_value());
  if (budget_manifest.failure)
    DP_EXPECT_TRUE(budget_manifest.failure->reason.find("budget") != std::string::npos);
  return dp::test::finish();
}
