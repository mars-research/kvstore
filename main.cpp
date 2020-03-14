#include <pthread.h>
#include <errno.h>
#include <fstream>
#include <boost/program_options.hpp>
#include <time.h>
#include <chrono>
#include <ctime>
#include "kmer_data.cpp"
#include "skht.h"
// #include "timestamp.h"
#include "numa.hpp"
#include "test_config.h"
#include "shard.h"
#include "libfipc.h"
#include "data_types.h"

/*/proc/cpuinfo*/
#define CPUFREQ_MHZ (2200.0)
static const float one_cycle_ns = ((float)1000 / CPUFREQ_MHZ);

/* Numa config */
Numa n;
std::vector<numa_node> nodes = n.get_node_config();

/* default config */
const Configuration def =
	{
		.kmer_create_data_base = 524288,
		.kmer_create_data_mult = 1,
		.kmer_create_data_uniq = 1048576,
		.num_threads = 10,
		.read_write_kmers = 1,
		.kmer_files_dir = std::string("/local/devel/pools/million/39/"),
		.alphanum_kmers = true,
		.numa_split = false,
		.stats_file = std::string(""),
		.ht_file = std::string("")};

/* global config */
Configuration config;

/* Thread stats */
struct thread_stats
{
	uint32_t thread_idx;	   // set before calling create_shards
	Shard *shard;			   // to be set by create_shards
	uint64_t insertion_cycles; //to be set by create_shards
	uint64_t find_cycles;
	uint64_t ht_fill;
	uint64_t ht_capacity;
#ifdef CALC_STATS
	uint64_t num_reprobes;
	uint64_t num_memcpys;
	uint64_t num_memcmps;
	uint64_t num_hashcmps;
	uint64_t num_queue_flushes;
	double avg_distance_from_bucket;
	uint64_t max_distance_from_bucket;
#endif /*CALC_STATS*/
};

typedef SimpleKmerHashTable skht_map;
std::string outfile;

static uint64_t ready = 0;
static uint64_t ready_threads = 0;

void *create_shards(void *arg)
{

	thread_stats *td = (thread_stats *)arg;
	uint64_t start, end;

	// printf("[INFO] Thread %u. Creating new shard\n", td->thread_idx);

	Shard *s = (Shard *)memalign(FIPC_CACHE_LINE_SIZE, sizeof(Shard));
	td->shard = s;
	td->shard->shard_idx = td->thread_idx;
	create_data(td->shard);

	size_t HT_SIZE = config.kmer_create_data_uniq;
	size_t POOL_SIZE = (config.kmer_create_data_base *
						config.kmer_create_data_mult);

	// printf("hashtable size: %lu\n", HT_SIZE);
	// printf("kmer pool size: %lu\n", POOL_SIZE);

	/* Create hash table */
	skht_map skht_ht(HT_SIZE);

	fipc_test_FAI(ready_threads);
	while (!ready)
		fipc_test_pause();
	// fipc_test_mfence();

	/*	Begin insert loop */
	start = RDTSC_START();
	for (size_t i = 0; i < POOL_SIZE; i++)
	{
		// printf("%lu: ", i);
		bool res = skht_ht.insert((base_4bit_t *)&td->shard->kmer_big_pool[i]);
		if (!res)
		{
			printf("FAIL\n");
		}
	}
	skht_ht.flush_queue();
	end = RDTSCP();
	td->insertion_cycles = (end - start);
	printf("[INFO] Thread %u: Inserts complete\n", td->thread_idx);
	/*	End insert loop	*/

	/*	Begin find loop	*/
	start = RDTSC_START();
	for (size_t i = 0; i < POOL_SIZE; i++)
	{
		Kmer_r *k = skht_ht.find((base_4bit_t *)&td->shard->kmer_big_pool[i]);
		//std::cout << *k << std::endl;
	}
	end = RDTSCP();
	td->find_cycles = (end - start);
	printf("[INFO] Thread %u: Finds complete\n", td->thread_idx);
	/*	End find loop	*/

	/* Write to file */
	if (!config.ht_file.empty())
	{
		std::string outfile = config.ht_file + std::to_string(td->thread_idx);
		printf("[INFO] Thread %u: Printing to file: %s\n", td->thread_idx,
			   outfile.c_str());
		ofstream f;
		f.open(config.ht_file + std::to_string(td->thread_idx));
		Kmer_r *ht = skht_ht.hashtable;

		for (size_t i = 0; i < skht_ht.get_capacity(); i++)
		{
			if (ht[i].kmer_count > 0)
			{
				f << ht[i] << std::endl;
			}
		}
	}

	td->ht_fill = skht_ht.get_fill();
	td->ht_capacity = skht_ht.get_capacity();

	printf("[INFO] Thread %u. HT fill: %lu of %lu (%f %) \n",
		   td->thread_idx, skht_ht.get_fill(), skht_ht.get_capacity(),
		   (double)skht_ht.get_fill() / skht_ht.get_capacity() * 100);
	printf("[INFO] Thread %u. HT max_kmer_count: %lu\n", td->thread_idx,
		   skht_ht.get_max_count());

#ifdef CALC_STATS
	td->num_reprobes = skht_ht.num_reprobes;
	td->num_memcmps = skht_ht.num_memcmps;
	td->num_memcpys = skht_ht.num_memcpys;
	td->num_queue_flushes = skht_ht.num_queue_flushes;
	td->num_hashcmps = skht_ht.num_hashcmps;
	td->avg_distance_from_bucket = (double)(skht_ht.sum_distance_from_bucket / HT_SIZE);
	td->max_distance_from_bucket = skht_ht.max_distance_from_bucket;
#endif

	fipc_test_FAD(ready_threads);

	return NULL;
}

void print_stats(thread_stats *all_td, uint32_t num_shards)
{
	uint64_t kmer_big_pool_size_per_shard =
		(config.kmer_create_data_base * config.kmer_create_data_mult);
	uint64_t total_kmer_big_pool_size = (kmer_big_pool_size_per_shard *
										 num_shards);

	uint64_t kmer_small_pool_size_per_shard = config.kmer_create_data_uniq;
	uint64_t total_kmer_small_pool_size = (kmer_small_pool_size_per_shard *
										   num_shards);

	uint64_t all_total_cycles = 0;
	double all_total_time_ns = 0;
	// uint64_t all_total_reprobes = 0;

	uint64_t all_total_find_cycles = 0;
	double all_total_find_time_ns = 0;

	for (size_t k = 0; k < num_shards; k++)
	{
		printf("Thread %2d: "
			   "%lu cycles per insertion, "
			   "%lu cycles per find"
#ifdef CALC_STATS
			   " [num_reprobes: %lu, "
			   "num_memcmps: %lu, "
			   "num_memcpys: %lu, "
			   "num_queue_flushes: %lu, "
			   "num_hashcmps: %lu, "
			   "max_distance_from_bucket: %lu, "
			   "avg_distance_from_bucket: %f]"
#endif /*CALC_STATS*/
			   "\n",
			   all_td[k].thread_idx, all_td[k].insertion_cycles / kmer_big_pool_size_per_shard, all_td[k].find_cycles / kmer_big_pool_size_per_shard
#ifdef CALC_STATS
			   ,
			   all_td[k].num_reprobes, all_td[k].num_memcmps, all_td[k].num_memcpys, all_td[k].num_queue_flushes, all_td[k].num_hashcmps, all_td[k].max_distance_from_bucket, all_td[k].avg_distance_from_bucket
#endif /*CALC_STATS*/
		);
		all_total_cycles += all_td[k].insertion_cycles;
		all_total_time_ns += (double)all_td[k].insertion_cycles * one_cycle_ns;
		// all_total_reprobes += all_td[k].num_reprobes;
		all_total_find_cycles += all_td[k].find_cycles;
		all_total_find_time_ns = (double)all_td[k].find_cycles * one_cycle_ns;
	}
	printf("===============================================================\n");
	printf("Average  : %lu cycles (%f ms) for %lu insertions (%lu cycles per insertion)\n",
		   all_total_cycles / num_shards,
		   (double)all_total_time_ns * one_cycle_ns / 1000,
		   kmer_big_pool_size_per_shard,
		   all_total_cycles / num_shards / kmer_big_pool_size_per_shard);
	printf("Average  : %lu cycles (%f ms) for %lu finds (%lu cycles per find)\n",
		   all_total_find_cycles / num_shards,
		   (double)all_total_find_time_ns * one_cycle_ns / 1000,
		   kmer_big_pool_size_per_shard,
		   all_total_find_cycles / num_shards / kmer_big_pool_size_per_shard);
	printf("===============================================================\n");
}

int spawn_shard_threads()
{

	cpu_set_t cpuset;
	int s;
	size_t i;

	pthread_t *threads = (pthread_t *)memalign(FIPC_CACHE_LINE_SIZE,
											   sizeof(pthread_t) * config.num_threads);
	thread_stats *all_td = (thread_stats *)memalign(FIPC_CACHE_LINE_SIZE,
													sizeof(thread_stats) * config.num_threads);
	memset(all_td, 0, sizeof(thread_stats *) * config.num_threads);

	if (config.numa_split)
	{
		size_t num_nodes = nodes.size();
		size_t shards_per_node = config.num_threads / num_nodes;

		/* TODO support uneven splits, and spills after splits :*/
		// size_t shards_per_node_spill = config.num_threads % num_nodes;

		for (size_t x = 0; x < num_nodes; x++)
		{
			for (size_t y = 0; y < shards_per_node; y++)
			{
				uint32_t tidx = shards_per_node * x + y;
				thread_stats *td = &all_td[tidx];
				td->thread_idx = tidx;
				s = pthread_create(&threads[tidx], NULL, create_shards,
								   (void *)td);
				if (s != 0)
				{
					printf("[ERROR] pthread_create: "
						   " Could not create create_shard thread");
					exit(-1);
				}
				CPU_ZERO(&cpuset);
				CPU_SET(nodes[x].cpu_list[y], &cpuset);
				pthread_setaffinity_np(threads[tidx], sizeof(cpu_set_t),
									   &cpuset);
				printf("[INFO] thread: %lu, set affinity: %u,\n", tidx,
					   nodes[x].cpu_list[y]);
			}
		}
	}

	else if (!config.numa_split)
	{
		for (size_t x = 0; x < config.num_threads; x++)
		{
			thread_stats *td = &all_td[x];
			td->thread_idx = x;
			s = pthread_create(&threads[x], NULL, create_shards, (void *)td);
			if (s != 0)
			{
				printf("[ERROR] pthread_create: Could not create create_shard \
					thread");
				exit(-1);
			}
			CPU_ZERO(&cpuset);
			size_t cpu_idx = x % nodes[0].cpu_list.size();
			CPU_SET(nodes[0].cpu_list[cpu_idx], &cpuset);
			pthread_setaffinity_np(threads[x], sizeof(cpu_set_t), &cpuset);
			printf("[INFO] thread: %lu, set affinity: %u,\n", x,
				   nodes[0].cpu_list[cpu_idx]);
		}
	}

	CPU_ZERO(&cpuset);
	/* last cpu of last node  */
	auto last_numa_node = nodes[n.get_num_nodes() - 1];
	CPU_SET(last_numa_node.cpu_list[last_numa_node.num_cpus - 1],
			&cpuset);
	sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

	while (ready_threads < config.num_threads)
		fipc_test_pause();

	// fipc_test_mfence();
	ready = 1;

	/* TODO thread join vs sync on atomic variable*/
	while (ready_threads)
		fipc_test_pause();

	print_stats(all_td, config.num_threads);

	free(threads);
	free(all_td);

	return 0;
}

int main(int argc, char *argv[])
{

	try
	{
		namespace po = boost::program_options;
		po::options_description desc("Program options");

		desc.add_options()("help", "produce help message")("mode", po::value<uint32_t>(&config.read_write_kmers)->default_value(def.read_write_kmers),
														   "1: Dry run, (--base, --mult, --uniq)\n"
														   "2: Read K-mers from disk,\n"
														   "3: Write K-mers to disk (--base, --mult, --uniq)")("base", po::value<uint64_t>(&config.kmer_create_data_base)->default_value(def.kmer_create_data_base), "Number of base K-mers")("mult", po::value<uint32_t>(&config.kmer_create_data_mult)->default_value(def.kmer_create_data_mult), "Base multiplier for K-mers")("uniq", po::value<uint64_t>(&config.kmer_create_data_uniq)->default_value(def.kmer_create_data_uniq),
																																																																																												  "Number of unique K-mers (to control the ratio)")("num_threads", po::value<uint32_t>(&config.num_threads)->default_value(def.num_threads),
																																																																																																									"Number of threads")("files_dir", po::value<std::string>(&config.kmer_files_dir)->default_value(def.kmer_files_dir),
																																																																																																														 "Directory of input files, files should be in format: '\\d{2}.bin'")("alphanum", po::value<bool>(&config.alphanum_kmers)->default_value(def.alphanum_kmers),
																																																																																																																															  "Use alphanum_kmers (for debugging)")("numa_split", po::value<bool>(&config.numa_split)->default_value(def.numa_split),
																																																																																																																																									"Split spwaning threads between numa nodes")("stats", po::value<std::string>(&config.stats_file)->default_value(def.stats_file),
																																																																																																																																																				 "Stats file name.")("outfile", po::value<std::string>(&config.ht_file)->default_value(def.ht_file),
																																																																																																																																																									 "Hashtable output file name.");

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		if (vm.count("help"))
		{
			cout << desc << "\n";
			return 1;
		}
	}
	catch (std::exception &e)
	{
		std::cout << e.what() << "\n";
		exit(-1);
	}

	spawn_shard_threads();
}
