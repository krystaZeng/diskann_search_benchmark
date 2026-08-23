#pragma once

namespace diskann_pipeline::search {

// Derived metrics intentionally have one owner: the Feature-specific Python
// Harness evaluate operation. This C++ marker prevents the search runner from
// acquiring a second evaluator implementation.
inline constexpr const char* kOfflineEvaluatorOwner =
    "diskann_harness.diskann_search_evaluation.evaluate";

}  // namespace diskann_pipeline::search
