#include "misc_lib.h"
#include "print_stats.h"
#include "sync.h"
#include "tests.hpp"

#include "hashtables/kvtypes.hpp"

// For extern theta_arg
#include "Application.hpp"


#include "distribution/pregen/mem.h"

namespace kmercounter {
struct kmer {
  char data[KMER_DATA_LENGTH];
};

extern void get_ht_stats(Shard *, BaseHashTable *);

// #define HT_TESTS_BATCH_LENGTH 32
#define HT_TESTS_BATCH_LENGTH 128
#define HT_TESTS_FIND_BATCH_LENGTH PREFETCH_FIND_QUEUE_SIZE

uint64_t HT_TESTS_HT_SIZE = (1 << 26);
uint64_t HT_TESTS_NUM_INSERTS;

#define HT_TESTS_MAX_STRIDE 2




#define MAX_THREADS 64

extern Configuration config;
volatile uint64_t* mem;

volatile uint8_t use_ready = 0;
volatile uint8_t clr_ready = 0;
void until_use_ready(uint8_t tid)
{
    while(tid!=0 && !use_ready){}
}
void until_ready(uint8_t tid)
{
    while(clr_ready!=tid){}
}

uint64_t start[MAX_THREADS];
uint64_t end[MAX_THREADS];
uint64_t data_size[MAX_THREADS];

uint64_t SynthTest::synth_run(BaseHashTable *ktable, uint8_t tid) {
  uint64_t count = 0;//HT_TESTS_NUM_INSERTS * tid;
  auto k = 0;
  auto i = 0UL;
  struct xorwow_state _xw_state;

  xorwow_init(&_xw_state);
  if (start == 0) count = 1;
  __attribute__((aligned(64))) struct kmer kmers[HT_TESTS_BATCH_LENGTH] = {0};
  __attribute__((aligned(64))) struct Item items[HT_TESTS_BATCH_LENGTH] = {0};
  __attribute__((aligned(64))) uint64_t keys[HT_TESTS_BATCH_LENGTH] = {0};

    uint64_t s = start[tid], e = end[tid], sum = 0;
    
    for (i = s; i < e; i++) {
        *((uint64_t *)&kmers[k].data) = count;//mem[i];
        *((uint64_t *)items[k].key()) = count;
        *((uint64_t *)items[k].value()) = count;
        //keys[k] = 
        sum += mem[i];//count;//mem[i];//
        // ktable->insert((void *)&items[k]);
        //ktable->insert((void *)&keys[k]);

        // ktable->insert_noprefetch((void *)&keys[k]);
        k = (k + 1) & (HT_TESTS_BATCH_LENGTH - 1);
        ++count;
    }

  printf("SUM: %lu\n", sum);
  // flush the last batch explicitly
  printf("%s calling flush queue\n", __func__);
  ktable->flush_queue();
  printf("%s: %p\n", __func__, ktable->find(&kmers[k]));
  return count;//i;
}

uint64_t SynthTest::synth_run_get(BaseHashTable *ktable, uint8_t start) {
  uint64_t count = HT_TESTS_NUM_INSERTS * start;
  auto i = 0, k = 0;
  uint64_t found = 0;
  if (start == 0) count = 1;

  __attribute__((aligned(64))) Aggr_KV items[HT_TESTS_FIND_BATCH_LENGTH] = {0};

  for (i = 0u; i < HT_TESTS_NUM_INSERTS; i++) {
    Aggr_KV *ret;
    // printf("[%s:%d] inserting i= %d, data %lu\n", __func__, start, i, count);
    items[k++].key = count++;

    if (k == HT_TESTS_FIND_BATCH_LENGTH) {
      // printf("%s, calling find_batch i = %d\n", __func__, i);
      found +=
          ktable->find_batch((uint64_t *)items, HT_TESTS_FIND_BATCH_LENGTH);
      k = 0;
    }
    // printf("\t count %lu | found -> %lu\n", count, found);
  }
  found += ktable->flush_find_queue();
  return found;
}

uint64_t seed2 = 123456789;
inline uint64_t PREFETCH_STRIDE = 64;
void SynthTest::synth_run_exec(Shard *sh, BaseHashTable *kmer_ht) {
  uint64_t num_inserts = 0;
  uint64_t t_start, t_end;

  printf("[INFO] Synth test run: thread %u, ht size: %lu, insertions: %lu\n",
         sh->shard_idx, HT_TESTS_HT_SIZE, HT_TESTS_NUM_INSERTS);

  for (auto i = 1; i < HT_TESTS_MAX_STRIDE; i++) {

    //Compute start and end range of data range for each thread
    start[sh->shard_idx] = ((double)sh->shard_idx/config.num_threads)*config.data_length;
    end[sh->shard_idx] = ((double)(sh->shard_idx+1)/config.num_threads)*config.data_length;
    data_size[sh->shard_idx] = end[sh->shard_idx] - start[sh->shard_idx];

    //freeze threads that arent thread 0 until use_ready is incremented
    until_use_ready(sh->shard_idx);
    if(sh->shard_idx == 0)
    {
        //allocate memory size with mmap and let other threads continue
        mem = allocate(config.data_length, config.data_range, config.theta, 0/*seed*/);
        ++use_ready;
    }

    //couldnt allocate memory
    if(!mem)
    {
      return;
    }

    //Precompute sum and data for pregeneration
    t_start = RDTSC_START();
    ZipfGen(config.data_range, config.theta, 0/*seed*/, sh->shard_idx, config.num_threads);
    t_end = RDTSCP();
    printf("[INFO] Sum %lu range in %lu cycles (%f ms) at rate of %lu cycles/element\n", config.data_range, t_end-t_start, (double)(t_end-t_start) * one_cycle_ns / 1000000.0, (t_end-t_start)/data_size[sh->shard_idx]);
    
    //pregenerate the indices/keys
    t_start = RDTSC_START();
    for (uint64_t j = start[sh->shard_idx]; j < end[sh->shard_idx]; ++j) 
    {
        mem[j] = next(); //TODO: modify to return key instead i.e. "keys[next()]
    }
    t_end = RDTSCP();
    printf("[INFO] Generate %lu elements in %lu cycles (%f ms) at rate of %lu cycles/element\n", data_size[sh->shard_idx], t_end-t_start, (double)(t_end-t_start) * one_cycle_ns / 1000000.0, (t_end-t_start)/data_size[sh->shard_idx]);

    t_start = RDTSC_START();
    // PREFETCH_QUEUE_SIZE = i;

    // PREFETCH_QUEUE_SIZE = 32;
    num_inserts = synth_run(kmer_ht, sh->shard_idx);
    t_end = RDTSCP();
    printf("[INFO] Inserted %lu elements in %lu cycles (%f ms) at rate of %lu cycles/element\n", num_inserts, t_end-t_start, (double)(t_end-t_start) * one_cycle_ns / 1000000.0, (t_end-t_start)/num_inserts);
    
    //Syncronize threads
    until_ready(sh->shard_idx);
    ++clr_ready;
    until_ready(config.num_threads);
    
    //free memeory allocated by thread 0
    if(sh->shard_idx == 0)
    {
        clear((uint64_t*) mem);
    }

    printf(
        "[INFO] Quick stats: thread %u, Batch size: %d, cycles per "
        "insertion:%lu \n",
        sh->shard_idx, i, (t_end - t_start) / num_inserts);

#ifdef CALC_STATS
    printf(" Reprobes %lu soft_reprobes %lu\n", kmer_ht->num_reprobes,
           kmer_ht->num_soft_reprobes);
#endif
  }
  sh->stats->insertion_cycles = (t_end - t_start);
  sh->stats->num_inserts = num_inserts;

  sleep(1);

  t_start = RDTSC_START();
  auto num_finds = synth_run_get(kmer_ht, sh->shard_idx);
  t_end = RDTSCP();

  if(num_finds)
  {
    printf("[INFO] thread %u | num_finds %lu | cycles per get: %lu\n",
          sh->shard_idx, num_finds, (t_end - t_start) / num_finds);
  }
  else
  {
    printf("[INFO] thread %u | num_finds 0 | cycles: %lu\n",
          sh->shard_idx, (t_end - t_start));
  }

#ifndef WITH_PAPI_LIB
  get_ht_stats(sh, kmer_ht);
  // kmer_ht->display();
#endif
}

}  // namespace kmercounter