#include "search/diskann/search_metrics_evaluator.hpp"

namespace diskann_pipeline::search {
static_assert(kOfflineEvaluatorOwner[0] != '\0',
              "offline evaluator ownership must remain explicit");
}  // namespace diskann_pipeline::search
