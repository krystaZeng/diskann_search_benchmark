// Compile the pinned, unmodified template definitions out of tree. This object
// precedes diskann_s on the link line, satisfying its partition.cpp definitions
// without extracting a second copy of partition.cpp.o from the static archive.
// No algorithm is copied, patched, seeded, or replaced here.
#include <immintrin.h>
#include KMEANS_NATIVE_SOURCE

template int shard_data_into_clusters_only_ids<float>(
    const std::string, float*, const size_t, const size_t, const size_t,
    std::string);
