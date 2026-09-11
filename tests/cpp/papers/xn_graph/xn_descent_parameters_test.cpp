#include "xn_descent_test_support.hpp"

#include "diskann_pipeline/checksum.hpp"

#include <fstream>
#include <limits>

namespace dp = diskann_pipeline;
namespace xn = diskann_pipeline::xn_descent;
namespace fixture = diskann_pipeline::xn_descent::test;

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void expect_invalid(fixture::Json value) {
  DP_EXPECT_THROW(xn::parse_parameters(value.dump()));
}

void expect_valid_profile(const std::string& profile, std::uint32_t lambda) {
  try {
    const auto parsed = xn::parse_parameters(
        fixture::effective_parameters_json(profile, 4, lambda));
    DP_EXPECT_EQ(xn::to_string(parsed.validation_profile), profile);
    DP_EXPECT_EQ(parsed.lambda, lambda);
  } catch (const std::exception& error) {
    dp::test::fail("profile parameters are accepted", __FILE__, __LINE__,
                   error.what());
  }
}

}  // namespace

int main() {
  const auto valid_text = fixture::effective_parameters_json();
  const auto valid = xn::parse_parameters(valid_text);
  DP_EXPECT_EQ(valid.validation_profile,
               xn::ValidationProfile::kSemanticSmall);
  DP_EXPECT_EQ(
      std::string(xn::kConvergenceRule),
      "full-state-fixed-point-or-three-round-c-g-stability-v1");
  DP_EXPECT_EQ(valid.seed, std::uint64_t{42});
  DP_EXPECT_EQ(xn::parse_parameters(xn::canonical_parameters_json(valid)).seed,
               std::uint64_t{42});

  auto authoring = fixture::Json::parse(valid_text);
  authoring.erase("seed");
  DP_EXPECT_EQ(xn::parse_parameters(authoring.dump()).seed, std::uint64_t{42});

  for (const auto* field : {"schema_version", "validation_profile", "omega",
                            "lambda", "alpha", "iteration_cap",
                            "prng_policy", "sampling_policy",
                            "tie_break_policy", "round_model",
                            "convergence_rule", "entry_point_rule",
                            "distance_implementation",
                            "reproduction_mode"}) {
    auto missing = fixture::Json::parse(valid_text);
    missing.erase(field);
    expect_invalid(std::move(missing));
  }

  auto invalid = fixture::Json::parse(valid_text);
  invalid["alpha"] = 0.999;
  expect_invalid(invalid);
  invalid["alpha"] = "nan";
  expect_invalid(invalid);
  invalid["alpha"] = std::numeric_limits<double>::infinity();
  expect_invalid(invalid);
  invalid = fixture::Json::parse(valid_text);
  invalid["omega"] = 0;
  expect_invalid(invalid);
  invalid["omega"] = -1;
  expect_invalid(invalid);
  invalid = fixture::Json::parse(valid_text);
  invalid["lambda"] = 0;
  expect_invalid(invalid);
  invalid = fixture::Json::parse(valid_text);
  invalid["iteration_cap"] = 0;
  expect_invalid(invalid);
  invalid = fixture::Json::parse(valid_text);
  invalid["seed"] = -1;
  expect_invalid(invalid);
  invalid["seed"] = std::numeric_limits<std::uint64_t>::max();
  DP_EXPECT_EQ(xn::parse_parameters(invalid.dump()).seed,
               std::numeric_limits<std::uint64_t>::max());

  expect_valid_profile("pipeline-compatible", 10);
  expect_valid_profile("pipeline-compatible", 11);
  expect_valid_profile("paper-style", 64);
  expect_valid_profile("paper-style", 63);

  auto unexpected = fixture::Json::parse(valid_text);
  unexpected["unknown"] = true;
  expect_invalid(unexpected);
  auto wrong_policy = fixture::Json::parse(valid_text);
  wrong_policy["round_model"] = "asynchronous";
  expect_invalid(wrong_policy);
  wrong_policy = fixture::Json::parse(valid_text);
  wrong_policy["convergence_rule"] = "normalized-c-r-g-fixed-point-v1";
  expect_invalid(wrong_policy);

  const auto project_root =
      std::filesystem::path(XN_DESCENT_TEST_PROJECT_ROOT);
  const auto historical_feature_schema =
      project_root /
      "specs/007-xn-descent-local-build/contracts/"
      "xn-descent-parameters.schema.json";
  const auto historical_runtime_schema =
      project_root /
      "contracts/papers/xn-graph-xn-descent-v1/schemas/"
      "xn-descent-parameters.schema.json";
  DP_EXPECT_EQ(read_text(historical_feature_schema),
               read_text(historical_runtime_schema));

  const auto feature_schema =
      std::filesystem::path(XN_DESCENT_TEST_PROJECT_ROOT) /
      "specs/007-xn-descent-local-build/contracts/"
      "xn-descent-parameters-v1.1.0.schema.json";
  const auto runtime_schema =
      std::filesystem::path(XN_DESCENT_TEST_PROJECT_ROOT) /
      "contracts/papers/xn-graph-xn-descent-v1/schemas/"
      "xn-descent-parameters-v1.1.0.schema.json";
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(feature_schema));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(runtime_schema));
  if (std::filesystem::is_regular_file(feature_schema) &&
      std::filesystem::is_regular_file(runtime_schema)) {
    DP_EXPECT_EQ(read_text(feature_schema), read_text(runtime_schema));
    const auto schema = fixture::Json::parse(read_text(feature_schema));
    DP_EXPECT_EQ(
        schema.at("$id").get<std::string>(),
        std::string(
            "https://diskann-methods.local/specs/007-xn-descent-local-build/"
            "contracts/xn-descent-parameters-v1.1.0.schema.json"));
    DP_EXPECT_EQ(schema.at("properties")
                     .at("schema_version")
                     .at("const")
                     .get<std::string>(),
                 std::string(xn::kParameterSchemaVersion));
    DP_EXPECT_TRUE(!schema.contains("allOf"));
  }

  const auto sources_path =
      project_root / "contracts/papers/xn-graph-xn-descent-v1/SOURCES.json";
  const auto sources = fixture::Json::parse(read_text(sources_path));
  const auto verify_source = [&](const std::string& source,
                                 const std::string& target,
                                 const std::filesystem::path& target_path) {
    const auto found = std::find_if(
        sources.at("entries").begin(), sources.at("entries").end(),
        [&](const auto& item) {
          return item.at("source") == source && item.at("target") == target;
        });
    DP_EXPECT_TRUE(found != sources.at("entries").end());
    if (found != sources.at("entries").end() &&
        std::filesystem::is_regular_file(target_path)) {
      DP_EXPECT_EQ(found->at("sha256").template get<std::string>(),
                   dp::sha256_file(target_path));
    }
  };
  verify_source(
      "specs/007-xn-descent-local-build/contracts/"
      "xn-descent-parameters.schema.json",
      "contracts/papers/xn-graph-xn-descent-v1/schemas/"
      "xn-descent-parameters.schema.json",
      historical_runtime_schema);
  verify_source(
      "specs/007-xn-descent-local-build/contracts/"
      "xn-descent-parameters-v1.1.0.schema.json",
      "contracts/papers/xn-graph-xn-descent-v1/schemas/"
      "xn-descent-parameters-v1.1.0.schema.json",
      runtime_schema);

  fixture::baseline_fixture::TemporaryDirectory temporary("xn-budgets");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  fixture::baseline_fixture::TestContext context(workspace, "positive");
  DP_EXPECT_TRUE(context.context.memory_budget_bytes > 0);
  DP_EXPECT_TRUE(context.context.thread_budget > 0);
  context.context.memory_budget_bytes = 0;
  DP_EXPECT_THROW(dp::validate_stage_context(context.context));
  context.context.memory_budget_bytes = 1;
  context.context.thread_budget = 0;
  DP_EXPECT_THROW(dp::validate_stage_context(context.context));
  return dp::test::finish();
}
