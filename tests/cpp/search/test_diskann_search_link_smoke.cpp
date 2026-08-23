#if DISKANN_SEARCH_LINUX_NATIVE_SMOKE

#include <immintrin.h>
#include "disk_utils.h"
#include "pq.h"
#include "pq_flash_index.h"

#include <cstdint>
#include <string>

int main() {
  const auto generate_pq = &diskann::generate_quantized_data<float>;
  const auto create_layout = &diskann::create_disk_layout<float>;
  const auto load =
      static_cast<int (diskann::PQFlashIndex<float>::*)(
          std::uint32_t, const char*, const char*, const char*)>(
          &diskann::PQFlashIndex<float>::load_from_separate_paths);

  return generate_pq != nullptr && create_layout != nullptr && load != nullptr
             ? 0
             : 1;
}

#else

#include <iostream>

int main() {
  std::cout << "diskann_search_link_smoke: Linux-only pending\n";
  return 77;
}

#endif
