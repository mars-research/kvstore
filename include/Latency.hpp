#ifndef _LATENCY_HPP
#define _LATENCY_HPP

#include <cpuid.h>
#include <plog/Log.h>
#include <x86intrin.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <thread>

#include "misc_lib.h"
#include "queues/section_queues.hpp"
#include "sync.h"
#include "xorwow.hpp"

namespace kmercounter {
using timer_type = std::uint32_t;

template <std::size_t capacity>
class alignas(64) LatencyCollector {
  static constexpr auto sentinel = std::numeric_limits<std::uint32_t>::max();

 public:
  ~LatencyCollector() {
    claim_lock->unlock();
    // for (const auto t : hack)
    //   PLOG_INFO << "[TIME]" << t;
  }

  void claim() {
    hack.reserve(1'000);
    if (!claim_lock->try_lock()) std::terminate();
  }

  std::uint32_t start() {
    if (reject_sample()) return sentinel;
    const auto id = allocate();
    start_timed(timers[id]);
    return id;
  }

  void end(std::uint32_t id) {
    //__builtin_trap();
    if (id == sentinel) __builtin_trap();
    std::uint64_t stop;
    stop_timed(stop);
    const auto time = stop - timers[id];
    free(id);
    static constexpr auto max_time = std::numeric_limits<timer_type>::max();
    push(time <= max_time ? static_cast<timer_type>(time) : max_time);
  }

  std::uint64_t sync_start() {
    if (reject_sample()) return sentinel;

    std::uint64_t b{};
    start_timed(b);

    return b;
  }

  void sync_end(std::uint64_t start) {
    if (start == sentinel) return;

    std::uint64_t a{};
    stop_timed(a);

    const auto stop = a;
    const auto time = stop - start;
    static constexpr auto max_time = std::numeric_limits<timer_type>::max();

    // ++hacky_count;
    // if (hacky_count > 1'000'000 && hacky_count < 1'000'000 + 1'000)
    //   hack.push_back(time);

    push(time <= max_time ? static_cast<timer_type>(time) : max_time);
  }

  void dump(const char* name, unsigned int id) {
    if (next_log_entry) {
      std::stringstream stream{};
      stream << "./latencies/" << name << '_' << id << ".dat";
      std::ofstream stats{stream.str().c_str()};
      stats.exceptions(stats.badbit | stats.failbit);
      for (auto i = 0u; i <= next_log_entry && i < log.size(); ++i) {
        const auto length = i < next_log_entry ? log.front().size() : next_slot;
        for (auto j = 0u; j < length; ++j)
          stats << static_cast<unsigned int>(log[i][j]) << "\n";
      }
    }
  }

 private:
  static constexpr bool use_rejections{false};

  std::uint64_t hacky_count{};
  std::vector<std::uint64_t> hack{};

  std::array<std::uint64_t, capacity> timers{};
  std::array<std::uint64_t, capacity / 64> bitmap{};

  std::array<std::array<timer_type, 64 / sizeof(timer_type)>, 4096> log{};
  std::uint8_t next_slot{};
  std::uint64_t next_log_entry{};

  std::shared_ptr<std::mutex> claim_lock{std::make_shared<std::mutex>()};

  xorwow_state rand_state{[] {
    xorwow_state state{};
    xorwow_init(&state);
    return state;
  }()};

  void start_timed(std::uint64_t& save) {
    unsigned int aux;
    //__cpuid(0, aux, aux, aux, aux);
    const auto time = __rdtsc();
    save = time;
  }

  void stop_timed(std::uint64_t& save) {
    //_mm_sfence();
    unsigned int aux;
    save = __rdtscp(&aux);
    //__cpuid(0, aux, aux, aux, aux);
  }

  bool reject_sample() {
    if constexpr (use_rejections) {
      constexpr auto pow2 = 10u;
      constexpr auto bitmask = (1ull << pow2) - 1;
      return xorwow(&rand_state) & bitmask;
    } else {
      return false;
    }
  }

  void free(std::uint32_t id) {
    const auto i = id >> 6;
    const auto bit = id & 0b111111;
    bitmap[i] &= ~(1ull << bit);
  }

  std::uint32_t allocate() {
    auto skipped = 0ull;
    for (; skipped < bitmap.size() && bitmap[skipped] == sentinel; ++skipped)
      ;

    if (skipped == bitmap.size()) std::terminate();
    const auto rightmost_zero = __builtin_ctzll(~bitmap[skipped]);
    bitmap[skipped] |= 1ull << rightmost_zero;

    return skipped * 64 + rightmost_zero;
  }

  void push(timer_type time) {
    if (next_slot == log.front().size()) {
      if (next_log_entry == log.size() - 1) return;

      next_slot = 0;
      ++next_log_entry;
    }

    log[next_log_entry][next_slot++] = time;
  }
};

constexpr auto pool_size = 2048;
using collector_type = LatencyCollector<pool_size>;
extern std::vector<collector_type> collectors;
extern std::mutex collector_lock;

}  // namespace kmercounter

#endif
