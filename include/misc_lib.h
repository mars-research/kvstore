#ifndef _MISC_LIB_H
#define _MISC_LIB_H

#include "data_types.h"

extern "C" {
#include "fcntl.h"
#include "stdio.h"
#include "sys/mman.h"
#include "sys/stat.h"
}

uint64_t get_file_size(const char* fn);
uint64_t round_down(uint64_t n, uint64_t m);
uint64_t round_up(uint64_t n, uint64_t m);
uint64_t calc_num_kmers(uint64_t l, uint8_t k);
int find_last_N(const char* c);
void __attribute__((optimize("O0"))) touchpages(char *fmap, size_t sz);

#endif  //_MISC_LIB_H