#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if defined(__APPLE__)
// The pinned headers require Linux-only libaio and GNU strerror_r semantics.
// The normative Linux build includes the real headers below; this declaration-only
// branch lets the macOS red test reach the linker without altering upstream.
template <typename T>
int partition_with_ram_budget(std::string, double, double, std::size_t,
                              std::string, std::size_t);
template <typename T>
int retrieve_shard_data_from_ids(std::string, std::string, std::string);
namespace diskann {
int merge_shards(const std::string&, const std::string&, const std::string&,
                 const std::string&, std::uint64_t, std::uint32_t,
                 const std::string&, const std::string&, bool,
                 const std::string&);
template <typename T, typename TagT = std::uint32_t,
          typename LabelT = std::uint32_t>
class Index {
 public:
  void build(const char*, std::size_t, const std::vector<TagT>&);
  void save(const char*, bool);
};
}  // namespace diskann
#else
#include <immintrin.h>
#include "disk_utils.h"
#include "index.h"
#include "partition.h"
#endif

namespace {

using Index = diskann::Index<float, std::uint32_t, std::uint32_t>;
using Partition = int (*)(std::string, double, double, std::size_t,
                          std::string, std::size_t);
using Retrieve = int (*)(std::string, std::string, std::string);
using Merge = int (*)(const std::string&, const std::string&,
                      const std::string&, const std::string&, std::uint64_t,
                      std::uint32_t, const std::string&, const std::string&,
                      bool, const std::string&);
using Build = void (Index::*)(const char*, std::size_t,
                             const std::vector<std::uint32_t>&);
using Save = void (Index::*)(const char*, bool);

Partition volatile partition_symbol = &partition_with_ram_budget<float>;
Retrieve volatile retrieve_symbol = &retrieve_shard_data_from_ids<float>;
Merge volatile merge_symbol = &diskann::merge_shards;
Build volatile build_symbol = static_cast<Build>(&Index::build);
Save volatile save_symbol = static_cast<Save>(&Index::save);

}  // namespace

int main() {
  return partition_symbol != nullptr && retrieve_symbol != nullptr &&
                 merge_symbol != nullptr && build_symbol != nullptr &&
                 save_symbol != nullptr
             ? 0
             : 1;
}
