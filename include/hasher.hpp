#ifndef _HASHER_HPP
#define _HASHER_HPP

#include <cstdint>

#include "fnv/fnv.h"
#include "wyhash/wyhash.h"
#include "xxHash/xxhash.h"
#include "cityhash/src/city.h"

namespace kmercounter {
class Hasher {
public:

  uint64_t operator()(const void* buff, uint64_t len) {
    uint64_t hash_val;
#if defined(CITY_HASH)
    hash_val = CityHash64((const char *)buff, len);
#elif defined(FNV_HASH)
    hash_val = state_ = fnv_64_buf(buff, len, state_);
#elif defined(XX_HASH)
    hash_val = XXH64(buff, len, 0);
#elif defined(XX_HASH_3)
    hash_val = XXH3_64bits(buff, len);
#else
    static_assert(false, "Hasher is not specified.");
#endif
    return hash_val;
  }

private:
#if defined(FNV_HASH)
  uint64_t state_ = FNV1_64_INIT;
#endif
};

} // namespace kmercounter
#endif // _HASHER_HPP