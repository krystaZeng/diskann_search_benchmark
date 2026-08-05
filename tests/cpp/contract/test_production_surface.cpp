#include "diskann_pipeline/registry.hpp"

#include "support/test_assert.hpp"

#include <type_traits>

namespace dp = diskann_pipeline;

int main() {
  static_assert(!std::is_default_constructible_v<dp::ImplementationRegistry>);
  static_assert(std::is_constructible_v<dp::ImplementationRegistry,
                                        dp::RegistryAdmissionPolicy>);
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
  registry.seal();
  const auto descriptors = registry.enumerate();
  DP_EXPECT_TRUE(descriptors.empty());
  for (const auto& descriptor : descriptors) {
    DP_EXPECT_TRUE(descriptor.implementation_id.find("fake") ==
                   std::string::npos);
    DP_EXPECT_TRUE(descriptor.implementation_id.find("adapter") ==
                   std::string::npos);
  }
  return dp::test::finish();
}
