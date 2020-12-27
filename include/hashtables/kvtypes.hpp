#ifndef __KV_TYPES_HPP__
#define __KV_TYPES_HPP__

#include "types.hpp"

namespace kmercounter {

struct Kmer_base {
  Kmer_s kmer;
  uint16_t count;
} PACKED;

struct Kmer_base_cas {
  Kmer_s kmer;
  uint8_t count;
  bool occupied;
} PACKED;

using Kmer_base_t = struct Kmer_base;
using Kmer_base_cas_t = struct Kmer_base_cas;

struct Kmer_KV_cas {
  Kmer_base_cas_t kb;        // 20 + 2 bytes
  uint64_t kmer_hash;        // 8 bytes
  volatile char padding[2];  // 2 bytes

  friend std::ostream &operator<<(std::ostream &strm, const Kmer_KV_cas &k) {
    return strm << std::string(k.kb.kmer.data, KMER_DATA_LENGTH) << " : "
                << k.kb.count;
  }
} PACKED;

struct Kmer_KV {
  Kmer_base_t kb;            // 20 + 2 bytes
  uint64_t kmer_hash;        // 8 bytes
  volatile char padding[2];  // 2 bytes

  friend std::ostream &operator<<(std::ostream &strm, const Kmer_KV &k) {
    // return strm << std::string(k.kb.kmer.data, KMER_DATA_LENGTH) << " : "
    return strm << *(uint64_t *)k.kb.kmer.data << " : " << k.kb.count;
  }

  inline void *data() { return this->kb.kmer.data; }

  inline void *key() { return this->kb.kmer.data; }

  inline void *value() { return NULL; }

  inline void insert_item(const void *from, size_t len) {
    const char *kmer_data = reinterpret_cast<const char *>(from);
    memcpy(this->kb.kmer.data, kmer_data, this->key_length());
    this->kb.count += 1;
  }

  inline bool compare_key(const void *from) {
    const char *kmer_data = reinterpret_cast<const char *>(from);
    return !memcmp(this->kb.kmer.data, kmer_data, this->key_length());
  }

  inline void update_value(const void *from, size_t len) {
    this->kb.count += 1;
  }

  inline uint16_t get_value() const { return this->kb.count; }

  inline constexpr size_t data_length() const { return sizeof(this->kb.kmer); }

  inline constexpr size_t key_length() const { return sizeof(this->kb.kmer); }

  inline constexpr size_t value_length() const { return sizeof(this->kb); }

  inline Kmer_KV get_empty_key() {
    Kmer_KV empty;
    memset(empty.kb.kmer.data, 0, sizeof(empty.kb.kmer.data));
    return empty;
  }

  inline bool is_empty() {
    Kmer_KV empty = this->get_empty_key();
    return !memcmp(this->key(), empty.key(), this->key_length());
  }

} PACKED;

static_assert(sizeof(Kmer_KV) % 32 == 0,
              "Sizeof Kmer_KV must be a multiple of 32");

// Kmer q in the hash hashtable
// Each q spills over a queue line for now, queue-align later
struct Kmer_queue {
  const void *data;
  uint32_t idx;  // TODO reduce size, TODO decided by hashtable size?
  uint8_t pad[4];
#ifdef COMPARE_HASH
  uint64_t key_hash;  // 8 bytes
#endif
} PACKED;

// TODO store org kmer idx, to check if we have wrappd around after reprobe
struct CAS_Kmer_queue_r {
  const void *data;
  uint32_t idx;  // TODO reduce size, TODO decided by hashtable size?
  uint8_t pad[4];
#ifdef COMPARE_HASH
  uint64_t key_hash;  // 8 bytes
#endif
} PACKED;

struct KVPair {
  uint64_t key;
  uint64_t value;
} PACKED;

struct Item {
  KVPair kvpair;

  friend std::ostream &operator<<(std::ostream &strm, const Item &item) {
    return strm << item.kvpair.key << " : " << item.kvpair.value;
  }

  inline constexpr size_t data_length() const { return sizeof(KVPair); }

  inline constexpr size_t key_length() const { return sizeof(kvpair.key); }

  inline constexpr size_t value_length() const { return sizeof(kvpair.value); }

  inline void insert_item(const void *from, size_t len) {
    const KVPair *kvpair = reinterpret_cast<const KVPair *>(from);
    this->kvpair = *kvpair;
  }

  inline bool compare_key(const void *from) {
    const KVPair *kvpair = reinterpret_cast<const KVPair *>(from);
    return this->kvpair.key == kvpair->key;
  }

  inline void update_value(const void *from, size_t len) {
    const KVPair *kvpair = reinterpret_cast<const KVPair *>(from);
    this->kvpair.value = kvpair->value;
  }

  inline void *data() { return &this->kvpair; }

  inline void *key() { return &this->kvpair.key; }

  inline void *value() { return &this->kvpair.value; }

  inline uint16_t get_value() const { return this->kvpair.value; }

  inline Item get_empty_key() {
    Item empty;
    empty.kvpair.key = empty.kvpair.value = 0;
    return empty;
  }

  inline bool is_empty() {
    Item empty = this->get_empty_key();
    return !memcmp(this->key(), empty.key(), this->key_length());
  }

} PACKED;

struct ItemQueue {
  const void *data;
  uint32_t idx;
#ifdef COMPARE_HASH
  uint64_t key_hash;  // 8 bytes
#endif
} PACKED;

}  // namespace kmercounter
#endif  // __KV_TYPES_HPP__