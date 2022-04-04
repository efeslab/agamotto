#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "level_hashing_printer.h"
// #include "//witcherAnnotation.h"

#define LEVEL_SIZE 2
#define RET_LEN VALUE_LEN

uint8_t* level_static_query_wrapper(level_hash* level, uint8_t* key) {
    uint8_t* get_value = level_static_query(level, key);
    if (get_value != NULL) {
      (void)strlen((char*)get_value);
    }
    return get_value;
}

uint64_t count_keys_by_level(level_hash* level, int l) {
  uint64_t nkeys = 0;
  // bucket
  uint64_t nbuckets = level->addr_capacity / (l + 1);
  for (uint64_t b = 0; b < nbuckets; ++b) {
    // slot
    for (uint64_t s = 0; s < ASSOC_NUM; ++s) {
      if (level->buckets[l][b].token[s] == 1) {
        nkeys++;
      }
    }
  }
  return nkeys;
}

uint64_t count_keys(level_hash *level) {
  return count_keys_by_level(level, 0) + count_keys_by_level(level, 1);
}

uint64_t reported_size_by_level(level_hash *level, int l) {
  return level->level_item_num[l];
}

uint64_t reported_size(level_hash *level) {
  return reported_size_by_level(level, 0) + reported_size_by_level(level, 1);
}

int sizes_match(level_hash *level) {
  return (count_keys_by_level(level, 0) == reported_size_by_level(level, 0))
  && (count_keys_by_level(level, 1) == reported_size_by_level(level, 1));
}

void run_op(char **op, level_hash *level, FILE *output_file) {
  uint8_t key[KEY_LEN];
  uint8_t value[VALUE_LEN];
  switch (op[0][0]) {
    case 'i':
      if (strcmp(op[1], " ") != 0) {
        snprintf((char*)key, KEY_LEN, "%s", op[1]);
      } else {
        key[0] = '\0';
      }

      if (strcmp(op[2], " ") != 0) {
        snprintf((char*)value, VALUE_LEN, "%s", op[2]);
      } else {
        value[0] = '\0';
      }

      //witcher_tx_begin();
      uint8_t wr_ret = level_insert(level, key, value);
      //witcher_tx_end();

      fprintf(output_file, "%d\n", wr_ret);
      break;
    case 'u':
      if (strcmp(op[1], " ") != 0) {
        snprintf((char*)key, KEY_LEN, "%s", op[1]);
      } else {
        key[0] = '\0';
      }

      if (strcmp(op[2], " ") != 0) {
        snprintf((char*)value, VALUE_LEN, "%s", op[2]);
      } else {
        value[0] = '\0';
      }

      //witcher_tx_begin();
      uint8_t update_ret = level_update(level, key, value);
      //witcher_tx_end();

      fprintf(output_file, "%d\n", update_ret);
      break;
    case 'd':
      if (strcmp(op[1], " ") != 0) {
        snprintf((char*)key, KEY_LEN, "%s", op[1]);
      } else {
        key[0] = '\0';
      }

      //witcher_tx_begin();
      uint8_t del_ret = level_delete(level, key);
      //witcher_tx_end();

      fprintf(output_file, "%d\n", del_ret);
      break;
    case 'e':
      //witcher_tx_begin();
      level_expand(level);
      //witcher_tx_end();

      fprintf(output_file, "\n");
      break;
    case 's':
      //witcher_tx_begin();
      level_shrink(level);
      //witcher_tx_end();

      fprintf(output_file, "\n");
      break;
    case 'g':
      if (strcmp(op[1], " ") != 0) {
        snprintf((char*)key, KEY_LEN, "%s", op[1]);
      } else {
        key[0] = '\0';
      }

      //witcher_tx_begin();
      uint8_t *rd_ret = level_static_query_wrapper(level, key);
      //witcher_tx_end();

      fprintf(output_file, "%s\n", rd_ret);
      break;
  }
}

void read_op_and_run(char *op_file_path,
                     int start_index,
                     int skip_index,
                     char *output_file_path,
                     level_hash *level) {
  FILE *output_file = fopen(output_file_path, "w");
  if (!output_file) {
    perror("output file open for writing");
    exit(1);
  }

  FILE *op_file = fopen(op_file_path, "r");
  if(!op_file) {
    perror("open op file for reading");
    exit(1);
  }

  char line[256];
  int count = 0;
  while (fgets(line, sizeof line, op_file) != NULL) {
    if (count < start_index || count == skip_index) {
      count++;
      continue;
    }

    char *op[3];
    char *p = strtok(line, ";");
    int i = 0;
    while (p != NULL && i < 3) {
      op[i++] = p;
      p = strtok (NULL, ";");
    }

    run_op(op, level, output_file);
    //print_level_hash(level);

    count++;
  }
  fclose(op_file);
  fclose(output_file);
}

int main(int argc, char *argv[]) {
  assert(argc== 8 || argc == 9);

  char *pmem_path = argv[1];
  size_t pmem_size_in_mib = atoi(argv[2]);
  char *layout_name = argv[3];

  char *op_file_path = argv[4];
  int start_index = atoi(argv[5]);
  int skip_index = atoi(argv[6]);

  char *output_file_path = argv[7];

  level_hash *level = level_init(pmem_path,
                                 pmem_size_in_mib,
                                 layout_name,
                                 LEVEL_SIZE);

  read_op_and_run(op_file_path,
                  start_index,
                  skip_index,
                  output_file_path,
                  level);

  return 0;
}
