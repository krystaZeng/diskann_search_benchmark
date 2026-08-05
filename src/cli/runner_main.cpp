#include "diskann_pipeline/runner_protocol.hpp"

#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <vector>

namespace diskann_pipeline {
void register_production_implementations(ImplementationRegistry& registry);
}

namespace {

class UnavailableVectors final : public diskann_pipeline::VectorAccessor {
 public:
  std::size_t dimension() const noexcept override { return 0; }
  void read(diskann_pipeline::GlobalNodeId,
            std::vector<float>&) const override {
    throw diskann_pipeline::PipelineError(
        diskann_pipeline::ErrorCategory::kCapabilityUnsatisfied,
        "production vector service is unavailable in Feature 001");
  }
};
class UnavailableDistance final : public diskann_pipeline::DistanceFunction {
 public:
  double evaluate(const std::vector<float>&,
                  const std::vector<float>&) const override {
    throw diskann_pipeline::PipelineError(
        diskann_pipeline::ErrorCategory::kCapabilityUnsatisfied,
        "production distance service is unavailable in Feature 001");
  }
};
class NullMetrics final : public diskann_pipeline::MetricsSink {
 public:
  void event(const std::string&) override {}
  void increment(const std::string&, std::uint64_t) override {}
};
class NeverCancelled final : public diskann_pipeline::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return false; }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6 || std::string(argv[2]) != "--request" ||
      std::string(argv[4]) != "--result") {
    std::cerr << "usage: diskann_pipeline_runner OPERATION --request FILE --result FILE\n";
    return static_cast<int>(diskann_pipeline::RunnerExitCode::kUsage);
  }
  diskann_pipeline::ImplementationRegistry registry(
      diskann_pipeline::RegistryAdmissionPolicy::kProduction);
  diskann_pipeline::register_production_implementations(registry);
  registry.seal();
  UnavailableVectors vectors;
  UnavailableDistance distance;
  NullMetrics metrics;
  NeverCancelled cancellation;
  diskann_pipeline::UnavailableCheckpointStore checkpoints;
  const auto bootstrap = diskann_pipeline::create_workspace(
      std::filesystem::temp_directory_path() / "diskann-production-runner");
  diskann_pipeline::TemporaryStorageRef temporary{
      bootstrap.temporary_root / "bootstrap-unpublished"};
  diskann_pipeline::StageContext context{
      bootstrap, 1, 1, temporary, &vectors, &distance, &checkpoints, &metrics,
      &cancellation};
  const auto result = diskann_pipeline::dispatch_runner_request(
      argv[3], registry, context);
  try {
    diskann_pipeline::write_runner_result(argv[5], result.result_json);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return static_cast<int>(diskann_pipeline::RunnerExitCode::kInternal);
  }
  return result.exit_code;
}
