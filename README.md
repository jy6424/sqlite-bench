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

## Saving SQL templates

Use `--save_sql=DIR` to export reusable SQL templates and benchmark metadata
without running the benchmark.

```sh
$ ./sqlite-bench \
    --benchmarks=fillseq,fillrandom,readrandom,readseq \
    --num=1000000 \
    --value_size=1024 \
    --save_sql=sql-plan
```

This writes files such as `schema.sql`, `fillseq.sql`, `fillrandom.sql`,
`readseq.sql`, `readrandom.sql`, and `benchmarks.tsv`. The SQL templates use
parameters; the sequential/random behavior is described in `benchmarks.tsv` and
must be reproduced by the runner when binding keys.

Use `--save_sql_full=DIR` to export complete SQL statements with key and value
blobs encoded as `X'...'` literals.

```sh
$ ./sqlite-bench \
    --benchmarks=fillseq,fillrandom,readrandom,readseq \
    --num=1000 \
    --value_size=1024 \
    --save_sql_full=sql-full
```

For large write benchmarks, expanded SQL files can be much larger than the raw
data because each byte is written as two hexadecimal characters.
