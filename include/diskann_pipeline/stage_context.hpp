#pragma once

#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/identifiers.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace diskann_pipeline {

struct WorkspaceRef {
  std::filesystem::path root;
  std::filesystem::path normal_root;
  std::filesystem::path temporary_root;
  std::filesystem::path manifest_root;
  std::filesystem::path report_root;
  std::filesystem::path quarantine_root;
};

struct TemporaryStorageRef {
  std::filesystem::path stage_root;
};

class VectorAccessor {
 public:
  virtual ~VectorAccessor() = default;
  virtual std::size_t dimension() const noexcept = 0;
  virtual void read(GlobalNodeId id,
                    std::vector<float>& destination) const = 0;
};

class DistanceFunction {
 public:
  virtual ~DistanceFunction() = default;
  virtual double evaluate(const std::vector<float>& lhs,
                          const std::vector<float>& rhs) const = 0;
};

class MetricsSink {
 public:
  virtual ~MetricsSink() = default;
  virtual void event(const std::string& name) = 0;
  virtual void increment(const std::string& counter,
                         std::uint64_t amount) = 0;
};

class CancellationToken {
 public:
  virtual ~CancellationToken() = default;
  virtual bool is_cancelled() const noexcept = 0;
};

class CheckpointStore {
 public:
  virtual ~CheckpointStore() = default;
  virtual bool available() const noexcept = 0;
  virtual void reject_unavailable_operation() const = 0;
};

class UnavailableCheckpointStore final : public CheckpointStore {
 public:
  bool available() const noexcept override { return false; }
  void reject_unavailable_operation() const override {
    throw PipelineError(ErrorCategory::kCheckpointUnavailable,
                        "checkpoint storage is unavailable in StageContext v1");
  }
};

struct StageContext {
  WorkspaceRef workspace;
  std::uint64_t memory_budget_bytes;
  std::uint32_t thread_budget;
  TemporaryStorageRef temporary_storage;
  VectorAccessor* vector_accessor;
  DistanceFunction* distance_function;
  CheckpointStore* checkpoint_store;
  MetricsSink* metrics_sink;
  CancellationToken* cancellation_token;
};

void validate_stage_context(const StageContext& context);

}  // namespace diskann_pipeline

