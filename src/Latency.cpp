#include "Latency.hpp"

namespace kvstore {
#ifdef LATENCY_COLLECTION
std::vector<LatencyCollector<pool_size>> collectors;
#endif
}  // namespace kvstore
