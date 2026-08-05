#include "fakes/fake_pipeline.hpp"

#include "diskann_pipeline/runner_protocol.hpp"
#include "diskann_pipeline/document_codec.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

nlohmann::json read_request(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Fake runner request is missing");
  }
  return nlohmann::json::parse(input);
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
  if (argc != 6 || std::string(argv[2]) != "--request" ||
      std::string(argv[4]) != "--result") {
    std::cerr << "usage: diskann_pipeline_fake_runner OPERATION --request FILE --result FILE\n";
    return static_cast<int>(diskann_pipeline::RunnerExitCode::kUsage);
  }
  try {
    const auto request = read_request(argv[3]);
    if (request.at("operation").get<std::string>() != argv[1]) {
      throw std::runtime_error("runner operation differs from request operation");
    }
    const auto root = request.at("workspace").at("root").get<std::string>();
    diskann_pipeline::test::FakeStageEnvironment environment("runner", root);
    const auto dataset =
        diskann_pipeline::test::materialize_fake_dataset(environment.workspace());
    if (request.contains("reuse_artifact")) {
      materialize_reuse_fixture(
          environment.workspace(), dataset,
          diskann_pipeline::parse_artifact_ref(
              request.at("reuse_artifact").dump()));
    }
    diskann_pipeline::ImplementationRegistry registry(
        diskann_pipeline::RegistryAdmissionPolicy::kTest);
    diskann_pipeline::test::register_fake_implementations(registry);
    registry.seal();
    auto context = environment.make_context(
        request.at("request_id").get<std::string>() + "-dispatch");
    const auto result = diskann_pipeline::dispatch_runner_request(
        argv[3], registry, context);
    diskann_pipeline::write_runner_result(argv[5], result.result_json);
    return result.exit_code;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return static_cast<int>(diskann_pipeline::RunnerExitCode::kInternal);
  }
}
