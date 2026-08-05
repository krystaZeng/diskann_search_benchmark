#include "fakes/fake_pipeline.hpp"

#include "support/test_assert.hpp"

namespace dp = diskann_pipeline;

int main() {
  dp::test::FakeStageEnvironment environment("stage-context");
  auto context = environment.make_context("unit");
  DP_EXPECT_TRUE(context.workspace.normal_root !=
                 context.workspace.quarantine_root);
  DP_EXPECT_EQ(context.vector_accessor->dimension(), std::size_t{2});
  std::vector<float> vector;
  context.vector_accessor->read(4, vector);
  DP_EXPECT_EQ(vector, std::vector<float>({1.0F, 1.0F}));
  DP_EXPECT_EQ(context.distance_function->evaluate({0.0F, 0.0F},
                                                   {1.0F, 1.0F}),
               2.0);
  context.metrics_sink->event("stage.started");
  context.metrics_sink->increment("nodes", 6);
  DP_EXPECT_EQ(environment.metrics().events().size(), std::size_t{1});
  DP_EXPECT_EQ(environment.metrics().counter("nodes"), std::uint64_t{6});
  DP_EXPECT_TRUE(!context.checkpoint_store->available());
  DP_EXPECT_THROW(context.checkpoint_store->reject_unavailable_operation());

  environment.cancellation().cancel_on_observation(2);
  DP_EXPECT_TRUE(!context.cancellation_token->is_cancelled());
  DP_EXPECT_TRUE(context.cancellation_token->is_cancelled());
  DP_EXPECT_EQ(environment.cancellation().observations(), std::size_t{2});

  return dp::test::finish();
}
