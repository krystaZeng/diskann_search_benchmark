#include "fakes/fake_pipeline.hpp"

#include "diskann_pipeline/runner_protocol.hpp"
#include "diskann_pipeline/document_codec.hpp"

#include <filesystem>
#include <cstdlib>
#include <array>
#include <nlohmann/json.hpp>
#include <iostream>

namespace {


// Test-only handshake: prove that a real child has entered Local Build after
// publishing Partition. The parent owns timeout/termination; no production IPC.
struct InterruptTrace { std::array<int, 3> factories{}, methods{}; };
class StartedPartition final : public diskann_pipeline::PartitionStrategy {
 public:
  explicit StartedPartition(InterruptTrace& trace) : trace_(trace) {}
  diskann_pipeline::StageResult run(const diskann_pipeline::ArtifactRef& dataset,
      const std::filesystem::path& parameters, diskann_pipeline::StageContext& context) override {
    ++trace_.methods[0];
    return diskann_pipeline::test::make_fake_partition()->run(dataset, parameters, context);
  }
 private: InterruptTrace& trace_;
};
class InterruptedLocal final : public diskann_pipeline::LocalGraphBuilder {
 public:
  explicit InterruptedLocal(InterruptTrace& trace) : trace_(trace) {}
  diskann_pipeline::StageResult build(const diskann_pipeline::ArtifactRef&,
      diskann_pipeline::PartitionId, const diskann_pipeline::LocalBuildConfig&,
      diskann_pipeline::StageContext&) override {
    ++trace_.methods[1];
    std::cout << nlohmann::json{{"checkpoint", "local-method-entered"},
                              {"factories", trace_.factories}, {"methods", trace_.methods}}.dump()
              << std::endl;
    std::cin.get(); // stdin is a pipe kept open by the test until termination.
    throw std::runtime_error("interrupt test handshake ended without termination");
  }
 private: InterruptTrace& trace_;
};
void register_interrupt_test(diskann_pipeline::ImplementationRegistry& registry,
                             InterruptTrace& trace) {
 namespace dp = diskann_pipeline;
 registry.register_partition(dp::test::fake_partition_descriptor(), [&] {
   ++trace.factories[0]; return std::make_unique<StartedPartition>(trace);
 });
 registry.register_local_build(dp::test::fake_local_graph_descriptor(), [&] {
   ++trace.factories[1]; return std::make_unique<InterruptedLocal>(trace);
 });
 registry.register_merge(dp::test::fake_merge_descriptor(), [&] {
   ++trace.factories[2]; return dp::test::make_fake_merge();
 });
}

void materialize_reuse_fixture(
    const diskann_pipeline::WorkspaceRef& workspace,
    const diskann_pipeline::ArtifactRef& dataset,
    const diskann_pipeline::ArtifactRef& expected) {
  const auto target = diskann_pipeline::resolve_normal_artifact_metadata(
      workspace, expected.metadata_path);
  if (std::filesystem::exists(target)) return;
  const auto temporary = diskann_pipeline::create_temporary_storage(
      workspace, "reuse-fixture");
  const auto candidate = diskann_pipeline::test::materialize_partition_candidate(
      temporary.stage_root, dataset);
  if (candidate.aggregate_checksum != expected.aggregate_checksum) {
    throw std::runtime_error("configured reuse fixture identity is not canonical");
  }
  std::filesystem::create_directories(target.parent_path().parent_path());
  std::error_code error;
  std::filesystem::rename(temporary.stage_root / "candidate",
                          target.parent_path(), error);
  if (error) {
    throw std::runtime_error("unable to publish Fake reuse fixture: " +
                             error.message());
  }
}

}  // namespace

int main(int argc, char** argv) {
  const bool registry_command =
      argc == 4 && std::string(argv[1]) == "registry" &&
      std::string(argv[2]) == "--result";
  const bool request_command =
      argc == 6 && std::string(argv[2]) == "--request" &&
      std::string(argv[4]) == "--result";
  if (!registry_command && !request_command) {
    std::cerr << "usage: diskann_pipeline_fake_runner OPERATION --request FILE --result FILE\n";
    return static_cast<int>(diskann_pipeline::RunnerExitCode::kUsage);
  }
  try {
    if (registry_command) {
      diskann_pipeline::ImplementationRegistry registry(
          diskann_pipeline::RegistryAdmissionPolicy::kTest);
      diskann_pipeline::test::register_fake_implementations(registry);
      registry.seal();
      const auto result = diskann_pipeline::dispatch_registry_request(registry);
      diskann_pipeline::write_runner_result(argv[3], result.result_json);
      return result.exit_code;
    }
    diskann_pipeline::ImplementationRegistry registry(
        diskann_pipeline::RegistryAdmissionPolicy::kTest);
    InterruptTrace trace;
    if (std::getenv("DISKANN_TEST_INTERRUPT_LOCAL")) register_interrupt_test(registry, trace);
    else diskann_pipeline::test::register_fake_implementations(registry);
    registry.seal();
    diskann_pipeline::RunnerRequestPrelude prelude;
    try {
      prelude = diskann_pipeline::inspect_runner_request(argv[3], argv[1]);
    } catch (const std::exception&) {
      diskann_pipeline::StageContext empty_context;
      const auto result = diskann_pipeline::dispatch_runner_request(
          argv[3], argv[1], registry, empty_context);
      diskann_pipeline::write_runner_result(argv[5], result.result_json);
      return result.exit_code;
    }
    const auto root = prelude.workspace_root;
    diskann_pipeline::reject_runner_write_target(argv[5]);
    diskann_pipeline::test::FakeStageEnvironment environment("runner", root);
    const auto dataset =
        diskann_pipeline::test::materialize_fake_dataset(environment.workspace());
    if (prelude.reuse_artifact) {
      materialize_reuse_fixture(
          environment.workspace(), dataset, *prelude.reuse_artifact);
    }
    auto context = environment.make_context(
        prelude.request_id + "-dispatch");
    const auto result = diskann_pipeline::dispatch_runner_request(
        argv[3], argv[1], registry, context);
    diskann_pipeline::write_runner_result(argv[5], result.result_json);
    return result.exit_code;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return static_cast<int>(diskann_pipeline::RunnerExitCode::kInternal);
  }
}
