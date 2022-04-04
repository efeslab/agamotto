#include "level_hashing.h"

void print_level_bucket(level_bucket* bucket) {
  int i = 0;
  printf("[");
  for (; i < ASSOC_NUM; i++) {
    printf("[%d,%s,%s]", bucket->token[i],
                         bucket->slot[i].key,
                         bucket->slot[i].value);
    if (i != ASSOC_NUM - 1) {
      printf(",");
    }
  }
  printf("]");
}

void print_level_buckets(level_bucket* bucket, uint64_t num_buckets) {
  int i = 0;
  printf("bucket_size=%lu:", num_buckets);
  printf("[");
  for (; i < num_buckets; ++i) {
    print_level_bucket(&bucket[i]);
    if (i != num_buckets - 1) {
      printf(",");
    }
  }
  printf("]\n");
}

void print_level_hash_statistics(level_hash* hash) {
  printf("[");
  printf("level_item_num[0]=%lu,", hash->level_item_num[0]);
  printf("level_item_num[1]=%lu,", hash->level_item_num[1]);
  printf("addr_capacity=%lu,", hash->addr_capacity);
  printf("total_capacity=%lu,", hash->total_capacity);
  printf("level_size=%lu,", hash->level_size);
  printf("level_expand_time=%hhu,", hash->level_expand_time);
  printf("resize_state=%hhu,", hash->resize_state);
  printf("f_seed=%lu,", hash->f_seed);
  printf("s_seed=%lu", hash->s_seed);
  printf("]\n");
}

void print_level_hash(level_hash* hash) {
  printf("Printing level hash:\n");

  // print meta info
  print_level_hash_statistics(hash);

  // print buckets in the top level
  printf("Printing level hash top level buckets:\n");
  uint64_t num_buckects_top_level = hash->addr_capacity;
  print_level_buckets(hash->buckets[0], num_buckects_top_level);

  // print buckets in the bottom level
  printf("Printing level hash bottom level buckets:\n");
  uint64_t num_buckects_bottom_level = hash->total_capacity - hash->addr_capacity;
  print_level_buckets(hash->buckets[1], num_buckects_bottom_level);

  // TODO print interim_level_buckets
}
