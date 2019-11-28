#include <iostream>
#include <cstring>
#include "city.h"
// #include "tbb/scalable_allocator.h"
// #include "tbb/tbb_allocator.h"
#include "data_types.h"
#include "kmer_class.h"

Kmer::Kmer() { }

Kmer::Kmer(const base_4bit_t *data, unsigned len){
		//_data = (base_4bit_t*) scalable_aligned_malloc(len, KMER_ALIGNMENT);
		this->kmer_len = len;
		this->_data = (base_4bit_t*) malloc(len);
		memcpy(_data, data, kmer_len);
		this->_hash = CityHash128((const char*)data, len);
		std::cout << "Kmer: " << this->_data << "," << this->kmer_len << std::endl;
     }

// hash and compare function - using cityhash
// struct Kmer_hash_compare {
// 	inline size_t hash( const Kmer &k ) {
// 		return k.hash();
// 	}
// 	//! True if strings are equal
// 	inline bool equal( const Kmer & x, const Kmer &y ) const {
// 		return x.hash() == y.hash();
// 	}
// };

// struct Kmer_hash {
// 	//! True if strings are equal
// 	inline bool operator()(const Kmer & k) const {
// 		return k.hash();
// 	}
// 	typedef ska::power_of_two_hash_policy hash_policy;
// };

// struct Kmer_equal {
// 	//! True if strings are equal
// 	inline bool operator()( const Kmer x, const Kmer y ) const {
// 		return x.hash() == y.hash();
// 	}
// };

// size_t hash_to_cpu(Kmer &k, uint32_t threadIdx, uint32_t numCons)
// {
// 	uint32_t queueNo = ((k.hash() * 11400714819323198485llu) >> 58) % numCons;
// 	//send_to_queue(queueNo, threadIdx, k);
// }