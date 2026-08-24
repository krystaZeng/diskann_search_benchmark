#include "search/diskann/pqflash_instrumented_runner.hpp"
#include "search/diskann/native_search_formats.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "support/test_assert.hpp"

#include <chrono>
#include <fstream>
#include <system_error>
#include <vector>

namespace ds = diskann_pipeline::search;
namespace dp = diskann_pipeline;

namespace {

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    path = std::filesystem::temp_directory_path() /
           ("search-native-alias-" +
            std::to_string(std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count()));
    std::filesystem::create_directories(path);
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  std::filesystem::path path;
};

void write_text(const std::filesystem::path& path, const std::string& value) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

struct FileFact {
  bool exists;
  std::uintmax_t bytes;
  std::string sha256;
};

FileFact fact(const std::filesystem::path& path) {
  return {std::filesystem::is_regular_file(path),
          std::filesystem::file_size(path), dp::sha256_file(path)};
}

void expect_same(const std::filesystem::path& path, const FileFact& expected) {
  const auto actual = fact(path);
  DP_EXPECT_EQ(actual.exists, expected.exists);
  DP_EXPECT_EQ(actual.bytes, expected.bytes);
  DP_EXPECT_EQ(actual.sha256, expected.sha256);
}

void native_alias_staging_is_non_mutating() {
  TemporaryDirectory temporary;
  const auto source_root = temporary.path / "source";
  const auto stage_root = temporary.path / "stage";
  std::filesystem::create_directories(source_root);
  std::filesystem::create_directories(stage_root);
  const std::vector<std::string> names{
      "disk.index", "medoids.bin", "pivots.bin", "compressed.bin"};
  std::vector<FileFact> before;
  for (const auto& name : names) {
    write_text(source_root / name, "payload-" + name);
    before.push_back(fact(source_root / name));
    const auto destination = stage_root / name;
    DP_EXPECT_EQ(ds::stage_native_payload_alias(source_root / name, destination),
                 ds::NativeAliasMethod::kHardLink);
    DP_EXPECT_TRUE(std::filesystem::equivalent(source_root / name, destination));
  }
  std::filesystem::remove_all(stage_root);
  for (std::size_t index = 0; index < names.size(); ++index) {
    expect_same(source_root / names[index], before[index]);
  }

  std::filesystem::create_directories(stage_root);
  const auto fallback = stage_root / "disk-copy.index";
  DP_EXPECT_EQ(
      ds::stage_native_payload_alias(
          source_root / names[0], fallback,
          [](const auto&, const auto&) {
            return std::make_error_code(std::errc::cross_device_link);
          }),
      ds::NativeAliasMethod::kCopy);
  DP_EXPECT_TRUE(!std::filesystem::equivalent(source_root / names[0], fallback));
  DP_EXPECT_EQ(dp::sha256_file(fallback), before[0].sha256);
  std::filesystem::remove_all(stage_root);
  expect_same(source_root / names[0], before[0]);
}

}  // namespace

int main() {
  ds::RawSearchPoint point;
  point.l_search = 100;
  point.k = 100;
  point.beamwidth = 2;
  point.repetition_index = 0;
  point.query_count = 1;
  point.completed_query_count = 1;
  point.batch_wall_clock_us = 1000;
  point.status = diskann_pipeline::StageStatus::kSucceeded;
  ds::PerQueryObservation query;
  query.query_row_index = 0;
  query.source_query_id = "opaque-0042";
  query.result_ids.resize(100);
  query.result_distances.resize(100);
  for (std::size_t index = 0; index < 100; ++index) {
    query.result_ids[index] = index;
    query.result_distances[index] = static_cast<float>(index);
  }
  query.total_us = 10.0;
  query.n_ios = 2;
  point.per_query.push_back(query);
  DP_EXPECT_TRUE(ds::validate_complete_raw_search_point(point, 1000000));
  DP_EXPECT_EQ(ds::derived_primary_cache_bytes(10000, 128, 4, 16),
               std::uint64_t{5800000});
  int invocations = 0;
  const auto observed = ds::capture_raw_search_point(
      point,
      [&](std::size_t, std::vector<std::uint64_t>& ids,
          std::vector<float>& distances, double& total_us,
          std::uint32_t& n_ios) {
        ++invocations;
        for (std::size_t index = 0; index < ids.size(); ++index) {
          ids[index] = index;
          distances[index] = static_cast<float>(index);
        }
        total_us = 10.0;
        n_ios = 2;
      });
  DP_EXPECT_EQ(invocations, 1);
  DP_EXPECT_TRUE(ds::validate_complete_raw_search_point(observed, 1000000));
  native_alias_staging_is_non_mutating();
  return diskann_pipeline::test::finish();
}
