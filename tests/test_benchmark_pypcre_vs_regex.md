# PyPcre vs regex benchmark

- Generated from `test_benchmark_pypcre_vs_regex.py`
- Python runtime: `3.13.12` free-threaded=`true`
- APIs: `module match`, `compiled match`, `module fullmatch`, `compiled fullmatch`, `compile`
- Iterations per thread: `1000`
- Benchmark rounds per scenario: `5`
- Thread counts: `1, 2, 4, 8, 16`
- Timing excludes thread startup by synchronizing all worker threads before the measured region.
- `pypcre (ms)` / `regex (ms)` are each-round averages: wall-clock elapsed time per round, with the highest and lowest rounds removed before averaging.
- `compiled match` / `compiled fullmatch` benchmark `c = compile(...); c.match(...) / c.fullmatch(...)`, with compile done before timing in each worker thread.
- `module match` / `module fullmatch` benchmark direct module calls and therefore include each engine's internal compile/cache path.
- No-match results for `match` / `fullmatch` are recorded as `error` instead of being counted as successful benchmark runs.
- `regex` free-threaded failures in uncompiled and compile paths are recorded as `error` instead of failing the whole benchmark.
- `pypcre_vs_regex` shows wall-clock ratio. `xN` means pypcre is N times faster; `x0.xx slower` means pypcre is slower.

## module match (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 5.732       | 7.403      | x1.29 faster    |
| 2       | 7.629       | 24.895     | x3.26 faster    |
| 4       | 11.628      | 74.553     | x6.41 faster    |
| 8       | 9.215       | 168.141    | x18.25 faster   |
| 16      | 17.958      | error      | n/a             |

regex errors:

- threads=16: UnicodeDecodeError: 'locale' codec can't decode byte 0xdc in position 0: decoding error

## compiled match (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 2.428       | 0.516      | x4.70 slower    |
| 2       | 4.065       | 1.066      | x3.81 slower    |
| 4       | 2.507       | 2.124      | x1.18 slower    |
| 8       | 5.012       | 9.259      | x1.85 faster    |
| 16      | 9.165       | 215.470    | x23.51 faster   |

## module fullmatch (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 3.311       | 3.619      | x1.09 faster    |
| 2       | 4.075       | 25.368     | x6.22 faster    |
| 4       | 4.722       | 71.344     | x15.11 faster   |
| 8       | 10.574      | 133.595    | x12.63 faster   |
| 16      | 19.609      | 3536.852   | x180.37 faster  |

## compiled fullmatch (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 1.792       | 0.524      | x3.42 slower    |
| 2       | 1.753       | 0.583      | x3.00 slower    |
| 4       | 3.112       | 1.970      | x1.58 slower    |
| 8       | 7.117       | 4.922      | x1.45 slower    |
| 16      | 6.479       | 426.202    | x65.78 faster   |

## compile (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 7.205       | 129.571    | x17.98 faster   |
| 2       | 15.010      | error      | n/a             |
| 4       | 13.972      | error      | n/a             |
| 8       | 23.303      | error      | n/a             |
| 16      | 71.944      | error      | n/a             |

regex errors:

- threads=2: RuntimeError: dictionary changed size during iteration
- threads=4: UnicodeDecodeError: 'locale' codec can't decode byte 0xde in position 0: decoding error
- threads=8: RuntimeError: dictionary changed size during iteration
- threads=16: UnicodeDecodeError: 'locale' codec can't decode byte 0xf2 in position 0: decoding error
