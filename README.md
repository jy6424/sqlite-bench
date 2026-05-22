# SQLite3 Benchmark [![CircleCI](https://circleci.com/gh/ukontainer/sqlite-bench.svg?style=shield)](https://circleci.com/gh/ukontainer/sqlite-bench)

A SQLite3 benchmark tool.
Most of the code comes from [LevelDB](https://github.com/google/leveldb).
This is C version of [benchmarks/db_bench_sqlite3.cc](https://github.com/google/leveldb/blob/main/benchmarks/db_bench_sqlite3.cc).

## Building

SQLite3 is included in the repository.

```sh
$ make
```

## Usage

```
$ ./sqlite-bench --help
Usage: ./sqlite-bench [OPTION]...
SQLite3 benchmark tool
[OPTION]
  --benchmarks=[BENCH]          specify benchmark
  --histogram={0,1}             record histogram
  --raw={0,1}                   output raw data
  --compression_ratio=DOUBLE    compression ratio
  --use_existing_db={0,1}       use existing database
  --num=INT                     number of entries
  --reads=INT                   number of reads
  --value_size=INT              value size
  --no_transaction              disable transaction
  --page_size=INT               page size
  --num_pages=INT               number of pages
  --WAL_enabled={0,1}           enable WAL
  --db=PATH                     path to location databases are created
  --save_sql=DIR                save SQL templates and benchmark metadata
  --save_sql_full=DIR           save expanded SQL statements
  --help                        show this help

[BENCH]
  fillseq       write N values in sequential key order in async mode
  fillseqsync   write N/100 values in sequential key order in sync mode
  fillseqbatch  batch write N values in sequential key order in async mode
  fillrandom    write N values in random key order in async mode
  fillrandsync  write N/100 values in random key order in sync mode
  fillrandbatch batch write N values in random key order in async mode
  overwrite     overwrite N values in random key order in async mode
  fillrand100K  write N/1000 100K values in random order in async mode
  fillseq100K   wirte N/1000 100K values in sequential order in async mode
  readseq       read N times sequentially
  readrandom    read N times in random order
  readrand100K  read N/1000 100K values in sequential order in async mode
```

## [DBS]
### sqlite3 , sqlite4 측정 방법

1. 컴파일 (sqlite4는 컴파일 된 파일 주소로 바꿔서 쓰기)

```sh
make sqlite3-runner

make sqlite4-runner \
    SQLITE4_CFLAGS="-I/path/to/sqlite4" \
    SQLITE4_LDFLAGS="/path/to/sqlite4/libsqlite4.a -llz4 -lz -lpthread -ldl -lm"
```

2. 실행 (1000만개, 100byte = 약 1GB workload)

```sh
./sqlite3-runner \
    --benchmarks=fillseq,fillrandom,readrandom,readseq \
    --num=10000000 \
    --value_size=1024 \
    --progress=1000000 \
    --db=sqlite3-runner.db
```

```sh
./sqlite4-runner \
    --benchmarks=fillseq,fillrandom,readrandom,readseq \
    --num=10000000 \
    --value_size=1024 \
    --progress=1000000 \
    --db=sqlite4-runner.db
```

맨 앞에 linux `time` 커맨드 붙여서 시간 측정 가능. (이때는 fillseq, fillrandom 등 각 workload를 따로따로 돌려서 측정해야 각각을 측정할 수 있음.)

[작동 방식]

prepare `REPLACE INTO test (key, value) VALUES (?, ?)` or
`SELECT * FROM test WHERE key = ?` once,

then repeat `bind/step/reset`.