#include "baseline/cpp_main/source_build_identity.hpp"

#include "diskann_pipeline/document_codec.hpp"
#include "support/test_assert.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace dp = diskann_pipeline;
namespace baseline = diskann_pipeline::cpp_main;

namespace {

std::string quote(const std::filesystem::path& value) {
  std::string result = "'";
  for (const char character : value.string()) {
    if (character == '\'') {
      result += "'\\''";
    } else {
      result += character;
    }
  }
  result += "'";
  return result;
}

void run(const std::string& command) {
  if (std::system(command.c_str()) != 0) {
    throw std::runtime_error("command failed: " + command);
  }
}

std::filesystem::path make_wrong_gitlink_fixture() {
  const auto root = std::filesystem::temp_directory_path() /
                    "cpp-main-wrong-gitlink-fixture";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root / "upstream" /
                                      "diskann-cpp-main");
  run("git init -q " + quote(root));
  run("git -C " + quote(root) + " config user.email test@example.invalid");
  run("git -C " + quote(root) + " config user.name test");
  std::ofstream(root / "upstream" / "diskann-cpp-main" / "ordinary.txt")
      << "not a gitlink\n";
  run("git -C " + quote(root) + " add upstream/diskann-cpp-main/ordinary.txt");
  run("git -C " + quote(root) + " commit -q -m fixture");
  return root;
}

std::filesystem::path make_pinned_gitlink_fixture(bool advance_head,
                                                  bool leave_dirty) {
  static unsigned sequence = 0;
  const auto root = std::filesystem::temp_directory_path() /
                    ("cpp-main-source-fixture-" +
                     std::to_string(sequence++));
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root / "upstream");
  run("git init -q " + quote(root));
  run("git -C " + quote(root) + " config user.email test@example.invalid");
  run("git -C " + quote(root) + " config user.name test");
  const auto real_upstream =
      std::filesystem::path(CPP_MAIN_TEST_REPOSITORY_ROOT) / "upstream" /
      "diskann-cpp-main";
  run("git clone -q --no-hardlinks " + quote(real_upstream) + " " +
      quote(root / "upstream" / "diskann-cpp-main"));
  run("git -C " + quote(root) + " add upstream/diskann-cpp-main");
  run("git -C " + quote(root) + " commit -q -m fixture");
  if (advance_head) {
    std::ofstream(root / "upstream" / "diskann-cpp-main" / "advance.txt")
        << "advance\n";
    run("git -C " + quote(root / "upstream" / "diskann-cpp-main") +
        " config user.email test@example.invalid");
    run("git -C " + quote(root / "upstream" / "diskann-cpp-main") +
        " config user.name test");
    run("git -C " + quote(root / "upstream" / "diskann-cpp-main") +
        " add advance.txt");
    run("git -C " + quote(root / "upstream" / "diskann-cpp-main") +
        " commit -q -m advance");
  }
  if (leave_dirty) {
    std::ofstream(root / "upstream" / "diskann-cpp-main" / "dirty.txt")
        << "dirty\n";
  }
  return root;
}

}  // namespace

int main() {
  const auto configured = baseline::configured_source_build_admission();
  const auto admitted = baseline::admit_source_and_build(configured);
  DP_EXPECT_EQ(admitted.source.repository,
               std::string{baseline::kPinnedRepository});
  DP_EXPECT_EQ(admitted.source.branch, std::string{baseline::kPinnedBranch});
  DP_EXPECT_EQ(admitted.source.commit, std::string{baseline::kPinnedCommit});
  DP_EXPECT_TRUE(!admitted.source.dirty_worktree);
  DP_EXPECT_TRUE(!admitted.upstream_library_sha256.empty());

  const auto manifest = baseline::make_build_manifest(
      admitted, "cpp-main-phase1-build", "2026-08-06T00:00:00Z",
      "2026-08-06T00:00:01Z");
  const auto encoded = dp::serialize_build_manifest(manifest);
  const auto decoded = dp::parse_build_manifest(encoded);
  DP_EXPECT_EQ(decoded.stage, std::string{"build"});
  DP_EXPECT_EQ(decoded.source.commit, std::string{baseline::kPinnedCommit});
  DP_EXPECT_TRUE(!decoded.toolchain.compiler_path.empty());
  DP_EXPECT_TRUE(!decoded.toolchain.compiler_version.empty());
  DP_EXPECT_TRUE(!decoded.toolchain.cmake_version.empty());
  DP_EXPECT_TRUE(!decoded.toolchain.generator.empty());
  DP_EXPECT_TRUE(!decoded.toolchain.build_type.empty());
  DP_EXPECT_TRUE(!decoded.toolchain.cmake_definitions.empty());
  DP_EXPECT_TRUE(!decoded.toolchain.compile_commands.empty());
  DP_EXPECT_TRUE(!decoded.toolchain.link_commands.empty());

  auto upstream_target = configured;
  upstream_target.upstream_binary_dir = configured.upstream_source_dir / "build";
  DP_EXPECT_THROW(baseline::admit_source_and_build(upstream_target));

  auto wrong_commit = configured;
  wrong_commit.expected_commit =
      "0000000000000000000000000000000000000000";
  DP_EXPECT_THROW(baseline::admit_source_and_build(wrong_commit));

  auto incomplete_build = configured;
  incomplete_build.build.compiler_path.clear();
  DP_EXPECT_THROW(baseline::admit_source_and_build(incomplete_build));

  const auto dirty_fixture = make_pinned_gitlink_fixture(false, true);
  auto dirty_source = configured;
  dirty_source.repository_root = dirty_fixture;
  dirty_source.upstream_source_dir =
      dirty_fixture / "upstream" / "diskann-cpp-main";
  dirty_source.upstream_binary_dir = dirty_fixture / "build";
  dirty_source.expected_repository =
      (std::filesystem::path(CPP_MAIN_TEST_REPOSITORY_ROOT) / "upstream" /
       "diskann-cpp-main")
          .string();
  DP_EXPECT_THROW(baseline::admit_source_and_build(dirty_source));

  const auto advanced_fixture = make_pinned_gitlink_fixture(true, false);
  auto wrong_head = configured;
  wrong_head.repository_root = advanced_fixture;
  wrong_head.upstream_source_dir =
      advanced_fixture / "upstream" / "diskann-cpp-main";
  wrong_head.upstream_binary_dir = advanced_fixture / "build";
  wrong_head.expected_repository = dirty_source.expected_repository;
  DP_EXPECT_THROW(baseline::admit_source_and_build(wrong_head));

  const auto synthetic = make_wrong_gitlink_fixture();
  auto wrong_gitlink = configured;
  wrong_gitlink.repository_root = synthetic;
  wrong_gitlink.upstream_source_dir =
      synthetic / "upstream" / "diskann-cpp-main";
  wrong_gitlink.upstream_binary_dir = synthetic / "build";
  DP_EXPECT_THROW(baseline::admit_source_and_build(wrong_gitlink));
  std::error_code ignored;
  std::filesystem::remove_all(dirty_fixture, ignored);
  std::filesystem::remove_all(advanced_fixture, ignored);
  std::filesystem::remove_all(synthetic, ignored);

  return dp::test::finish();
}
