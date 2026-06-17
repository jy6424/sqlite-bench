#include "sqlite3.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct Random {
  uint32_t seed_;
} Random;

typedef struct RandomGenerator {
  char *data_;
  size_t data_size_;
  int pos_;
} RandomGenerator;

static const char* db_path = "sqlite3-runner.db";
static const char* benchmarks_arg = "fillseq,fillrandom,readrandom,readseq";
static int num_arg = 1000000;
static int reads_arg = -1;
static int value_size_arg = 100;
static int progress_arg = 0;
static double compression_ratio_arg = 0.5;
static sqlite3* db = NULL;

static const char* kSchemaSql =
    "CREATE TABLE test (key blob, value blob, PRIMARY KEY (key));";
static const char* kWriteSql =
    "REPLACE INTO test (key, value) VALUES (?, ?)";
static const char* kReadSql =
    "SELECT * FROM test WHERE key = ?";

static uint64_t now_micros(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)(tv.tv_sec * 1000000 + tv.tv_usec);
}

static void die(const char* msg) {
  fprintf(stderr, "%s\n", msg);
  exit(1);
}

static void check_db(int rc, const char* what) {
  if (rc != SQLITE_OK) {
    fprintf(stderr, "%s failed: rc=%d", what, rc);
    if (db != NULL) fprintf(stderr, " msg=%s", sqlite3_errmsg(db));
    fprintf(stderr, "\n");
    exit(1);
  }
}

static void check_step(int rc, const char* what) {
  if (rc != SQLITE_DONE) {
    fprintf(stderr, "%s failed: rc=%d msg=%s\n", what, rc, sqlite3_errmsg(db));
    exit(1);
  }
}

static void rand_init(Random* rand, uint32_t s) {
  rand->seed_ = s & 0x7fffffffu;
  if (rand->seed_ == 0 || rand->seed_ == 2147483647L) rand->seed_ = 1;
}

static uint32_t rand_next(Random* rand) {
  static const uint32_t M = 2147483647L;
  static const uint64_t A = 16807;
  uint64_t product = rand->seed_ * A;
  rand->seed_ = (uint32_t)((product >> 31) + (product & M));
  if (rand->seed_ > M) rand->seed_ -= M;
  return rand->seed_;
}

static uint32_t rand_uniform(Random* rand, int n) {
  return rand_next(rand) % n;
}

static int* random_permutation(Random* rnd, int n) {
  int* keys = malloc(sizeof(int) * (size_t)n);
  if (keys == NULL) die("malloc failed");
  for (int i = 0; i < n; i++) keys[i] = i;
  for (int i = n - 1; i > 0; i--) {
    int j = (int)(rand_next(rnd) % (uint32_t)(i + 1));
    int tmp = keys[i];
    keys[i] = keys[j];
    keys[j] = tmp;
  }
  return keys;
}

static char* random_string(Random* rnd, int len) {
  char* dst = malloc((size_t)len + 1);
  if (dst == NULL) die("malloc failed");
  for (int i = 0; i < len; i++) dst[i] = (char)(' ' + rand_uniform(rnd, 95));
  dst[len] = '\0';
  return dst;
}

static char* compressible_string(Random* rnd, double compressed_fraction,
                                 size_t len) {
  int raw = (int)(len * compressed_fraction);
  if (raw < 1) raw = 1;
  char* raw_data = random_string(rnd, raw);
  size_t raw_data_len = strlen(raw_data);
  char* dst = malloc(len + raw_data_len + 1);
  if (dst == NULL) die("malloc failed");
  dst[0] = '\0';
  for (size_t pos = 0; pos < len; pos += raw_data_len) strcat(dst, raw_data);
  free(raw_data);
  return dst;
}

static void rand_gen_init(RandomGenerator* gen, double compression_ratio) {
  Random rnd;
  gen->data_ = malloc(1048576 + 100);
  if (gen->data_ == NULL) die("malloc failed");
  gen->data_size_ = 0;
  gen->pos_ = 0;
  gen->data_[0] = '\0';
  rand_init(&rnd, 301);
  while (gen->data_size_ < 1048576) {
    char* piece = compressible_string(&rnd, compression_ratio, 100);
    strcat(gen->data_, piece);
    gen->data_size_ += strlen(piece);
    free(piece);
  }
}

static char* rand_gen_generate(RandomGenerator* gen, int len) {
  if (gen->pos_ + len > (int)gen->data_size_) {
    gen->pos_ = 0;
    if (len >= (int)gen->data_size_) die("value_size is too large");
  }
  gen->pos_ += len;
  char* substr = malloc((size_t)len + 1);
  if (substr == NULL) die("malloc failed");
  strncpy(substr, gen->data_ + gen->pos_ - len, len);
  substr[len] = '\0';
  return substr;
}

static char* string_dup(const char* s) {
  char* copy = malloc(strlen(s) + 1);
  if (copy == NULL) die("malloc failed");
  strcpy(copy, s);
  return copy;
}

static void close_db(void) {
  if (db != NULL) {
    check_db(sqlite3_close(db), "sqlite3_close");
    db = NULL;
  }
}

static void open_db(void) {
  if (db == NULL) {
    check_db(sqlite3_open(db_path, &db), "sqlite3_open");
    check_db(sqlite3_exec(db, "PRAGMA synchronous = FULL", NULL, NULL, NULL),
             "PRAGMA synchronous");
    check_db(sqlite3_exec(db, "PRAGMA journal_mode = WAL", NULL, NULL, NULL),
             "PRAGMA journal_mode");
    check_db(sqlite3_exec(db, "PRAGMA locking_mode = EXCLUSIVE", NULL, NULL, NULL),
             "PRAGMA locking_mode");
  }
}

static void recreate_db(void) {
  close_db();
  unlink(db_path);
  open_db();
  check_db(sqlite3_exec(db, kSchemaSql, NULL, NULL, NULL), "schema");
}

static void run_one(const char* name, const char* sql, bool is_write,
                    bool is_random, int operations) {
  Random rnd;
  RandomGenerator gen = {0};
  int* random_keys = NULL;
  sqlite3_stmt* stmt = NULL;
  int64_t bytes = 0;

  if (is_write) {
    recreate_db();
    rand_gen_init(&gen, compression_ratio_arg);
  } else {
    open_db();
  }
  rand_init(&rnd, 301);
  if (is_write && is_random) {
    random_keys = random_permutation(&rnd, operations);
  }
  check_db(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), "sqlite3_prepare_v2");

  uint64_t start = now_micros();
  for (int i = 0; i < operations; i++) {
    int k = is_random ? (is_write ? random_keys[i] :
        (int)(rand_next(&rnd) % operations)) : i;
    char key[100];
    snprintf(key, sizeof(key), "%016d", k);
    check_db(sqlite3_bind_blob(stmt, 1, key, 16, SQLITE_TRANSIENT),
             "sqlite3_bind_blob(key)");
    if (is_write) {
      char* value = rand_gen_generate(&gen, value_size_arg);
      check_db(sqlite3_bind_blob(stmt, 2, value, value_size_arg,
                                 SQLITE_TRANSIENT),
               "sqlite3_bind_blob(value)");
      check_step(sqlite3_step(stmt), "sqlite3_step(write)");
      free(value);
      bytes += value_size_arg + 16;
    } else {
      int rc;
      while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {}
      check_step(rc, "sqlite3_step(read)");
    }
    check_db(sqlite3_clear_bindings(stmt), "sqlite3_clear_bindings");
    check_db(sqlite3_reset(stmt), "sqlite3_reset");
    if (progress_arg > 0 && (i + 1) % progress_arg == 0) {
      fprintf(stderr, "%s progress: %d/%d\n", name, i + 1, operations);
    }
  }
  uint64_t finish = now_micros();
  double seconds = (finish - start) / 1000000.0;
  double micros_per_op = (finish - start) / (double)(operations == 0 ? 1 : operations);
  if (bytes > 0) {
    fprintf(stderr, "%-12s : %11.3f micros/op; %6.1f MB/s\n",
            name, micros_per_op, (bytes / 1048576.0) / seconds);
  } else {
    fprintf(stderr, "%-12s : %11.3f micros/op;\n", name, micros_per_op);
  }
  check_db(sqlite3_finalize(stmt), "sqlite3_finalize");
  if (is_write) free(gen.data_);
  free(random_keys);
}

static void run_benchmark(const char* name) {
  int reads = reads_arg < 0 ? num_arg : reads_arg;
  if (!strcmp(name, "fillseq")) {
    run_one("fillseq", kWriteSql, true, false, num_arg);
  } else if (!strcmp(name, "fillrandom")) {
    run_one("fillrandom", kWriteSql, true, true, num_arg);
  } else if (!strcmp(name, "readrandom")) {
    run_one("readrandom", kReadSql, false, true, reads);
  } else if (!strcmp(name, "readseq")) {
    run_one("readseq", kReadSql, false, false, reads);
  } else if (strcmp(name, "")) {
    fprintf(stderr, "unknown benchmark '%s'\n", name);
    exit(1);
  }
}

static void usage(const char* argv0) {
  fprintf(stderr,
          "Usage: %s [--benchmarks=LIST] [--num=N] [--reads=N] "
          "[--value_size=N] [--progress=N] [--db=PATH]\n",
          argv0);
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--benchmarks=", 13) == 0) {
      benchmarks_arg = argv[i] + 13;
    } else if (strncmp(argv[i], "--num=", 6) == 0) {
      num_arg = atoi(argv[i] + 6);
    } else if (strncmp(argv[i], "--reads=", 8) == 0) {
      reads_arg = atoi(argv[i] + 8);
    } else if (strncmp(argv[i], "--value_size=", 13) == 0) {
      value_size_arg = atoi(argv[i] + 13);
    } else if (strncmp(argv[i], "--progress=", 11) == 0) {
      progress_arg = atoi(argv[i] + 11);
    } else if (strncmp(argv[i], "--compression_ratio=", 20) == 0) {
      compression_ratio_arg = atof(argv[i] + 20);
    } else if (strncmp(argv[i], "--db=", 5) == 0) {
      db_path = argv[i] + 5;
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  char* benchmarks = string_dup(benchmarks_arg);
  char* cursor = benchmarks;
  while (cursor != NULL) {
    char* sep = strchr(cursor, ',');
    if (sep != NULL) *sep = '\0';
    run_benchmark(cursor);
    cursor = sep == NULL ? NULL : sep + 1;
  }
  free(benchmarks);
  close_db();
  return 0;
}
