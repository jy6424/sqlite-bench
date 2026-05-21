// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "bench.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

static const char* kSchemaSql =
    "CREATE TABLE test (\n"
    "  key blob,\n"
    "  value blob,\n"
    "  PRIMARY KEY (key)\n"
    ");\n";

static const char* kWriteSql =
    "REPLACE INTO test (key, value) VALUES (?, ?);\n";

static const char* kReadSql =
    "SELECT * FROM test WHERE key = ?;\n";

enum SqlFullOp {
  SQL_FULL_WRITE,
  SQL_FULL_READ
};

enum SqlFullOrder {
  SQL_FULL_SEQUENTIAL,
  SQL_FULL_RANDOM
};

static void ensure_dir(const char* dir) {
  if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
    fprintf(stderr, "mkdir error for '%s': %s\n", dir, strerror(errno));
    exit(1);
  }
}

static char* path_join(const char* dir, const char* name) {
  size_t dir_len = strlen(dir);
  size_t name_len = strlen(name);
  bool need_slash = dir_len > 0 && dir[dir_len - 1] != '/';
  char* path = malloc(dir_len + name_len + (need_slash ? 2 : 1));
  if (path == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(1);
  }
  strcpy(path, dir);
  if (need_slash) {
    strcat(path, "/");
  }
  strcat(path, name);
  return path;
}

static void write_text_file(const char* dir, const char* name,
                            const char* contents) {
  char* path = path_join(dir, name);
  FILE* file = fopen(path, "w");
  if (file == NULL) {
    fprintf(stderr, "open error for '%s': %s\n", path, strerror(errno));
    exit(1);
  }
  fputs(contents, file);
  fclose(file);
  free(path);
}

static bool benchmark_requested(const char* target) {
  char* benchmarks = FLAGS_benchmarks;
  while (benchmarks != NULL) {
    char* sep = strchr(benchmarks, ',');
    size_t name_len = sep == NULL ? strlen(benchmarks) : (size_t)(sep - benchmarks);
    if (strlen(target) == name_len && !strncmp(benchmarks, target, name_len)) {
      return true;
    }
    benchmarks = sep == NULL ? NULL : sep + 1;
  }
  return false;
}

static void write_benchmark_sql(const char* dir, const char* name,
                                const char* sql) {
  char file_name[100];
  snprintf(file_name, sizeof(file_name), "%s.sql", name);
  write_text_file(dir, file_name, sql);
}

static void write_metadata_row(FILE* file, const char* name,
                               const char* sql_file, const char* op,
                               const char* key_order, int operations,
                               int value_size) {
  fprintf(file, "%s\t%s\t%s\t%s\t%d\t%d\t301\t%.17g\t%s\n",
          name, sql_file, op, key_order, operations, value_size,
          FLAGS_compression_ratio,
          FLAGS_transaction ? "transaction_enabled" : "transaction_disabled");
}

static void write_hex(FILE* file, const char* data, int len) {
  static const char kHex[] = "0123456789ABCDEF";
  for (int i = 0; i < len; i++) {
    unsigned char c = (unsigned char)data[i];
    fputc(kHex[c >> 4], file);
    fputc(kHex[c & 0x0f], file);
  }
}

static FILE* open_output_file(const char* dir, const char* name) {
  char* path = path_join(dir, name);
  FILE* file = fopen(path, "w");
  if (file == NULL) {
    fprintf(stderr, "open error for '%s': %s\n", path, strerror(errno));
    exit(1);
  }
  free(path);
  return file;
}

static void write_full_sql_file(const char* dir, const char* name,
                                int op, int order, int operations,
                                int value_size, Random* rnd,
                                RandomGenerator* gen) {
  char file_name[100];
  snprintf(file_name, sizeof(file_name), "%s.sql", name);

  FILE* file = open_output_file(dir, file_name);
  for (int i = 0; i < operations; i++) {
    int k = order == SQL_FULL_SEQUENTIAL ? i : (int)(rand_next(rnd) % operations);
    char key[100];
    snprintf(key, sizeof(key), "%016d", k);

    if (op == SQL_FULL_WRITE) {
      char* value = rand_gen_generate(gen, value_size);
      fprintf(file, "REPLACE INTO test (key, value) VALUES (X'");
      write_hex(file, key, 16);
      fprintf(file, "', X'");
      write_hex(file, value, value_size);
      fprintf(file, "');\n");
      free(value);
    } else {
      fprintf(file, "SELECT * FROM test WHERE key = X'");
      write_hex(file, key, 16);
      fprintf(file, "';\n");
    }
  }
  fclose(file);
}

void save_sql_plan() {
  int reads = FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads;
  bool want_fillseq = benchmark_requested("fillseq");
  bool want_fillrandom = benchmark_requested("fillrandom");
  bool want_readseq = benchmark_requested("readseq");
  bool want_readrandom = benchmark_requested("readrandom");

  ensure_dir(FLAGS_save_sql);

  write_text_file(FLAGS_save_sql, "schema.sql", kSchemaSql);
  write_text_file(FLAGS_save_sql, "write.sql", kWriteSql);
  write_text_file(FLAGS_save_sql, "read.sql", kReadSql);

  if (want_fillseq) {
    write_benchmark_sql(FLAGS_save_sql, "fillseq", kWriteSql);
  }
  if (want_fillrandom) {
    write_benchmark_sql(FLAGS_save_sql, "fillrandom", kWriteSql);
  }
  if (want_readseq) {
    write_benchmark_sql(FLAGS_save_sql, "readseq", kReadSql);
  }
  if (want_readrandom) {
    write_benchmark_sql(FLAGS_save_sql, "readrandom", kReadSql);
  }

  char* metadata_path = path_join(FLAGS_save_sql, "benchmarks.tsv");
  FILE* metadata = fopen(metadata_path, "w");
  if (metadata == NULL) {
    fprintf(stderr, "open error for '%s': %s\n", metadata_path, strerror(errno));
    exit(1);
  }

  fprintf(metadata,
          "name\tsql_file\top\tkey_order\toperations\tvalue_size\t"
          "random_seed\tcompression_ratio\ttransaction\n");
  if (want_fillseq) {
    write_metadata_row(metadata, "fillseq", "fillseq.sql", "write",
                       "sequential", FLAGS_num, FLAGS_value_size);
  }
  if (want_fillrandom) {
    write_metadata_row(metadata, "fillrandom", "fillrandom.sql", "write",
                       "random", FLAGS_num, FLAGS_value_size);
  }
  if (want_readseq) {
    write_metadata_row(metadata, "readseq", "readseq.sql", "read",
                       "sequential", reads, FLAGS_value_size);
  }
  if (want_readrandom) {
    write_metadata_row(metadata, "readrandom", "readrandom.sql", "read",
                       "random", reads, FLAGS_value_size);
  }
  fclose(metadata);
  free(metadata_path);

  write_text_file(
      FLAGS_save_sql,
      "README.md",
      "# SQLite Benchmark SQL Plan\n"
      "\n"
      "These files contain reusable SQL templates for the selected benchmarks.\n"
      "The sequential/random behavior is not encoded in SQL; it is described in\n"
      "`benchmarks.tsv` and must be reproduced by the runner when binding keys.\n"
      "\n"
      "Key format is a 16-byte decimal string such as `0000000000000000`.\n"
      "Random key generation uses the LevelDB benchmark random generator with\n"
      "seed `301`.\n");

  fprintf(stderr, "Saved SQL plan to %s\n", FLAGS_save_sql);
}

void save_sql_full() {
  int reads = FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads;
  Random rnd;
  RandomGenerator gen;

  rand_init(&rnd, 301);
  rand_gen_init(&gen, FLAGS_compression_ratio);
  ensure_dir(FLAGS_save_sql_full);
  write_text_file(FLAGS_save_sql_full, "schema.sql", kSchemaSql);

  char* benchmarks = FLAGS_benchmarks;
  while (benchmarks != NULL) {
    char* sep = strchr(benchmarks, ',');
    size_t name_len = sep == NULL ? strlen(benchmarks) : (size_t)(sep - benchmarks);
    char name[100];
    if (name_len >= sizeof(name)) {
      fprintf(stderr, "benchmark name too long\n");
      exit(1);
    }
    strncpy(name, benchmarks, name_len);
    name[name_len] = '\0';

    if (!strcmp(name, "fillseq")) {
      write_full_sql_file(FLAGS_save_sql_full, "fillseq", SQL_FULL_WRITE,
                          SQL_FULL_SEQUENTIAL, FLAGS_num, FLAGS_value_size,
                          &rnd, &gen);
    } else if (!strcmp(name, "fillrandom")) {
      write_full_sql_file(FLAGS_save_sql_full, "fillrandom", SQL_FULL_WRITE,
                          SQL_FULL_RANDOM, FLAGS_num, FLAGS_value_size,
                          &rnd, &gen);
    } else if (!strcmp(name, "readseq")) {
      write_full_sql_file(FLAGS_save_sql_full, "readseq", SQL_FULL_READ,
                          SQL_FULL_SEQUENTIAL, reads, FLAGS_value_size,
                          &rnd, &gen);
    } else if (!strcmp(name, "readrandom")) {
      write_full_sql_file(FLAGS_save_sql_full, "readrandom", SQL_FULL_READ,
                          SQL_FULL_RANDOM, reads, FLAGS_value_size,
                          &rnd, &gen);
    } else if (strcmp(name, "")) {
      fprintf(stderr, "warning: skipping expanded SQL for '%s'\n", name);
    }

    benchmarks = sep == NULL ? NULL : sep + 1;
  }

  write_text_file(
      FLAGS_save_sql_full,
      "README.md",
      "# Expanded SQLite Benchmark SQL\n"
      "\n"
      "These files contain complete SQL statements with key/value blobs encoded\n"
      "as `X'...'` literals. Large write benchmarks can generate multi-GB SQL\n"
      "files because each value byte is written as two hex characters.\n");

  fprintf(stderr, "Saved expanded SQL to %s\n", FLAGS_save_sql_full);
}
