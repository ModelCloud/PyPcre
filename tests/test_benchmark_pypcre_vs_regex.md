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
- `regex` free-threaded failures in uncompiled and compile paths are recorded as `error` instead of failing the whole benchmark.
- `pypcre_vs_regex` shows wall-clock ratio. `xN` means pypcre is N times faster; `x0.xx slower` means pypcre is slower.

## module match (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 7.261       | 3.671      | x0.51 slower    |
| 2       | 5.931       | 25.636     | x4.32 faster    |
| 4       | 5.119       | 60.251     | x11.77 faster   |
| 8       | 10.781      | 244.942    | x22.72 faster   |
| 16      | 340.426     | 7059.720   | x20.74 faster   |

## compiled match (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 1.222       | 0.580      | x0.47 slower    |
| 2       | 2.739       | 0.823      | x0.30 slower    |
| 4       | 2.341       | 1.405      | x0.60 slower    |
| 8       | 3.628       | 4.679      | x1.29 faster    |
| 16      | 11.493      | 364.789    | x31.74 faster   |

## module fullmatch (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 2.566       | 3.742      | x1.46 faster    |
| 2       | 2.800       | 19.935     | x7.12 faster    |
| 4       | 5.828       | 64.451     | x11.06 faster   |
| 8       | 8.725       | 138.969    | x15.93 faster   |
| 16      | 22.434      | 2824.129   | x125.89 faster  |

## compiled fullmatch (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 1.344       | 0.743      | x0.55 slower    |
| 2       | 2.581       | 0.531      | x0.21 slower    |
| 4       | 4.374       | 2.160      | x0.49 slower    |
| 8       | 4.413       | 6.612      | x1.50 faster    |
| 16      | 6.511       | 308.368    | x47.36 faster   |

## compile (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 6.517       | 127.855    | x19.62 faster   |
| 2       | 7.871       | error      | n/a             |
| 4       | 10.396      | error      | n/a             |
| 8       | 27.438      | error      | n/a             |
| 16      | 74.439      | error      | n/a             |

regex errors:

- threads=2: UnicodeDecodeError: 'locale' codec can't decode byte 0xc2 in position 0: decoding error
- threads=4: UnicodeDecodeError: 'locale' codec can't decode byte 0xc0 in position 0: decoding error
- threads=8: ValueError: embedded null character
- threads=16: Error: unsupported locale setting
