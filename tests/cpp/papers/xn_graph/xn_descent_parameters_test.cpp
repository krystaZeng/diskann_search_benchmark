#include "xn_descent_test_support.hpp"

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

}  // namespace

int main() {
  const auto valid_text = fixture::effective_parameters_json();
  const auto valid = xn::parse_parameters(valid_text);
  DP_EXPECT_EQ(valid.validation_profile,
               xn::ValidationProfile::kSemanticSmall);
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

  const auto pipeline = xn::parse_parameters(
      fixture::effective_parameters_json("pipeline-compatible", 4, 10));
  DP_EXPECT_EQ(pipeline.validation_profile,
               xn::ValidationProfile::kPipelineCompatible);
  expect_invalid(fixture::Json::parse(
      fixture::effective_parameters_json("pipeline-compatible", 4, 11)));
  const auto paper = xn::parse_parameters(
      fixture::effective_parameters_json("paper-style", 40, 64));
  DP_EXPECT_EQ(paper.lambda, std::uint32_t{64});
  expect_invalid(fixture::Json::parse(
      fixture::effective_parameters_json("paper-style", 40, 63)));

  auto unexpected = fixture::Json::parse(valid_text);
  unexpected["unknown"] = true;
  expect_invalid(unexpected);
  auto wrong_policy = fixture::Json::parse(valid_text);
  wrong_policy["round_model"] = "asynchronous";
  expect_invalid(wrong_policy);

  const auto feature_schema =
      std::filesystem::path(XN_DESCENT_TEST_PROJECT_ROOT) /
      "specs/007-xn-descent-local-build/contracts/"
      "xn-descent-parameters.schema.json";
  const auto runtime_schema =
      std::filesystem::path(XN_DESCENT_TEST_PROJECT_ROOT) /
      "contracts/papers/xn-graph-xn-descent-v1/schemas/"
      "xn-descent-parameters.schema.json";
  DP_EXPECT_EQ(read_text(feature_schema), read_text(runtime_schema));

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
