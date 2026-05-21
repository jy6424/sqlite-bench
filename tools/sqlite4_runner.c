#include "sqlite4.h"

#include <errno.h>
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

static const char* plan_dir = NULL;
static const char* db_path = "sqlite4-bench.db";
static sqlite4* db = NULL;

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
  if (rc != SQLITE4_OK) {
    fprintf(stderr, "%s failed: rc=%d", what, rc);
    if (db != NULL) {
      fprintf(stderr, " msg=%s", sqlite4_errmsg(db));
    }
    fprintf(stderr, "\n");
    exit(1);
  }
}

static void rand_init(Random* rand, uint32_t s) {
  rand->seed_ = s & 0x7fffffffu;
  if (rand->seed_ == 0 || rand->seed_ == 2147483647L) {
    rand->seed_ = 1;
  }
}

static uint32_t rand_next(Random* rand) {
  static const uint32_t M = 2147483647L;
  static const uint64_t A = 16807;
  uint64_t product = rand->seed_ * A;
  rand->seed_ = (uint32_t)((product >> 31) + (product & M));
  if (rand->seed_ > M) {
    rand->seed_ -= M;
  }
  return rand->seed_;
}

static uint32_t rand_uniform(Random* rand, int n) {
  return rand_next(rand) % n;
}

static char* random_string(Random* rnd, int len) {
  char* dst = malloc((size_t)len + 1);
  if (dst == NULL) die("malloc failed");
  for (int i = 0; i < len; i++) {
    dst[i] = (char)(' ' + rand_uniform(rnd, 95));
  }
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
  for (size_t pos = 0; pos < len; pos += raw_data_len) {
    strcat(dst, raw_data);
  }
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

static char* path_join(const char* dir, const char* name) {
  size_t dir_len = strlen(dir);
  size_t name_len = strlen(name);
  bool need_slash = dir_len > 0 && dir[dir_len - 1] != '/';
  char* path = malloc(dir_len + name_len + (need_slash ? 2 : 1));
  if (path == NULL) die("malloc failed");
  strcpy(path, dir);
  if (need_slash) strcat(path, "/");
  strcat(path, name);
  return path;
}

static char* read_file(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "open error for '%s': %s\n", path, strerror(errno));
    exit(1);
  }
  if (fseek(file, 0, SEEK_END) != 0) die("fseek failed");
  long size = ftell(file);
  if (size < 0) die("ftell failed");
  rewind(file);
  char* data = malloc((size_t)size + 1);
  if (data == NULL) die("malloc failed");
  if (fread(data, 1, (size_t)size, file) != (size_t)size) die("fread failed");
  data[size] = '\0';
  fclose(file);
  return data;
}

static void close_db(void) {
  if (db != NULL) {
    check_db(sqlite4_close(db, 0), "sqlite4_close");
    db = NULL;
  }
}

static void open_db(void) {
  if (db == NULL) {
    check_db(sqlite4_open(0, db_path, &db), "sqlite4_open");
  }
}

static void recreate_db(void) {
  close_db();
  unlink(db_path);
  open_db();
  char* schema_path = path_join(plan_dir, "schema.sql");
  char* schema = read_file(schema_path);
  check_db(sqlite4_exec(db, schema, 0, 0), "schema");
  free(schema);
  free(schema_path);
}

static void run_one(const char* name, const char* sql_file, const char* op,
                    const char* key_order, int operations, int value_size,
                    int seed, double compression_ratio) {
  bool is_write = strcmp(op, "write") == 0;
  bool is_random = strcmp(key_order, "random") == 0;
  Random rnd;
  RandomGenerator gen;
  sqlite4_stmt* stmt = NULL;

  if (is_write) {
    recreate_db();
    rand_gen_init(&gen, compression_ratio);
  } else {
    open_db();
  }
  rand_init(&rnd, (uint32_t)seed);

  char* sql_path = path_join(plan_dir, sql_file);
  char* sql = read_file(sql_path);
  check_db(sqlite4_prepare(db, sql, -1, &stmt, 0), "sqlite4_prepare");
  if (stmt == NULL) die("empty SQL statement");

  uint64_t start = now_micros();
  int done = 0;
  int64_t bytes = 0;
  for (int i = 0; i < operations; i++) {
    int k = is_random ? (int)(rand_next(&rnd) % operations) : i;
    char key[100];
    snprintf(key, sizeof(key), "%016d", k);

    check_db(sqlite4_bind_blob(stmt, 1, key, 16, SQLITE4_TRANSIENT, 0),
             "sqlite4_bind_blob(key)");
    if (is_write) {
      char* value = rand_gen_generate(&gen, value_size);
      check_db(sqlite4_bind_blob(stmt, 2, value, value_size, SQLITE4_TRANSIENT, 0),
               "sqlite4_bind_blob(value)");
      int rc = sqlite4_step(stmt);
      if (rc != SQLITE4_DONE) check_db(rc, "sqlite4_step(write)");
      free(value);
      bytes += value_size + 16;
    } else {
      int rc;
      while ((rc = sqlite4_step(stmt)) == SQLITE4_ROW) {}
      if (rc != SQLITE4_DONE) check_db(rc, "sqlite4_step(read)");
    }

    check_db(sqlite4_clear_bindings(stmt), "sqlite4_clear_bindings");
    check_db(sqlite4_reset(stmt), "sqlite4_reset");
    done++;
  }
  uint64_t finish = now_micros();

  double seconds = (finish - start) / 1000000.0;
  double micros_per_op = (finish - start) / (double)(done == 0 ? 1 : done);
  if (bytes > 0) {
    fprintf(stderr, "%-12s : %11.3f micros/op; %6.1f MB/s\n",
            name, micros_per_op, (bytes / 1048576.0) / seconds);
  } else {
    fprintf(stderr, "%-12s : %11.3f micros/op;\n", name, micros_per_op);
  }

  check_db(sqlite4_finalize(stmt), "sqlite4_finalize");
  free(sql);
  free(sql_path);
  if (is_write) free(gen.data_);
}

static void run_plan(void) {
  char* plan_path = path_join(plan_dir, "benchmarks.tsv");
  FILE* file = fopen(plan_path, "r");
  if (file == NULL) {
    fprintf(stderr, "open error for '%s': %s\n", plan_path, strerror(errno));
    exit(1);
  }

  char line[4096];
  if (fgets(line, sizeof(line), file) == NULL) die("empty benchmarks.tsv");
  while (fgets(line, sizeof(line), file) != NULL) {
    char* fields[9];
    int n = 0;
    char* tok = strtok(line, "\t\r\n");
    while (tok != NULL && n < 9) {
      fields[n++] = tok;
      tok = strtok(NULL, "\t\r\n");
    }
    if (n < 9) continue;
    run_one(fields[0], fields[1], fields[2], fields[3], atoi(fields[4]),
            atoi(fields[5]), atoi(fields[6]), atof(fields[7]));
  }
  fclose(file);
  free(plan_path);
  close_db();
}

static void usage(const char* argv0) {
  fprintf(stderr, "Usage: %s --plan=DIR --db=PATH\n", argv0);
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--plan=", 7) == 0) {
      plan_dir = argv[i] + 7;
    } else if (strncmp(argv[i], "--db=", 5) == 0) {
      db_path = argv[i] + 5;
    } else {
      usage(argv[0]);
      return 1;
    }
  }
  if (plan_dir == NULL) {
    usage(argv[0]);
    return 1;
  }

  run_plan();
  return 0;
}
