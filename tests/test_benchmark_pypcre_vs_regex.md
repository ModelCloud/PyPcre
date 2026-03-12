# PyPcre vs regex benchmark

- Generated from `test_benchmark_pypcre_vs_regex.py`
- Python runtime: `3.13.12` free-threaded=`true`
- APIs: `module match`, `compiled match`, `module fullmatch`, `compiled fullmatch`, `compile`
- Iterations per thread: `1000`
- Benchmark rounds per scenario: `3`
- Thread counts: `1, 2, 4, 8, 16`
- Timing excludes thread startup by synchronizing all worker threads before the measured region.
- `pypcre (ms)` / `regex (ms)` are each-round averages: wall-clock elapsed time per round, with the highest and lowest rounds removed before averaging.
- `compiled match` / `compiled fullmatch` benchmark `c = compile(...); c.match(...) / c.fullmatch(...)`, with compile done before timing in each worker thread.
- `module match` / `module fullmatch` benchmark direct module calls and therefore include each engine's internal compile/cache path.
- `regex` free-threaded failures in uncompiled and compile paths are recorded as `error` instead of failing the whole benchmark.
- `pypcre_vs_regex` shows `faster xx.x%`, `slower xx.x%`, `same`, or `n/a` based on wall-clock time.

## module match (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 3.650       | 3.739      | faster 2.4%     |
| 2       | 9.668       | 24.571     | faster 60.7%    |
| 4       | 10.468      | 78.579     | faster 86.7%    |
| 8       | 9.416       | 170.528    | faster 94.5%    |
| 16      | 1270.424    | 5811.005   | faster 78.1%    |

## compiled match (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 2.760       | 0.419      | slower 559.1%   |
| 2       | 1.692       | 1.747      | faster 3.2%     |
| 4       | 1.868       | 1.785      | slower 4.7%     |
| 8       | 7.215       | 3.386      | slower 113.1%   |
| 16      | 9.162       | 163.819    | faster 94.4%    |

## module fullmatch (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 4.133       | 3.675      | slower 12.5%    |
| 2       | 4.359       | 27.760     | faster 84.3%    |
| 4       | 3.164       | 70.542     | faster 95.5%    |
| 8       | 7.958       | 195.705    | faster 95.9%    |
| 16      | 22.974      | 4744.302   | faster 99.5%    |

## compiled fullmatch (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 1.293       | 0.461      | slower 180.4%   |
| 2       | 1.420       | 2.347      | faster 39.5%    |
| 4       | 1.847       | 1.602      | slower 15.3%    |
| 8       | 3.363       | 4.796      | faster 29.9%    |
| 16      | 8.791       | 13.209     | faster 33.4%    |

## compile (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 7.011       | 140.107    | faster 95.0%    |
| 2       | 9.284       | error      | n/a             |
| 4       | 9.999       | error      | n/a             |
| 8       | 20.614      | error      | n/a             |
| 16      | 40.089      | error      | n/a             |

regex errors:

- threads=2: UnicodeDecodeError: 'locale' codec can't decode byte 0xb2 in position 0: decoding error
- threads=4: UnicodeDecodeError: 'locale' codec can't decode byte 0xb0 in position 0: decoding error
- threads=8: UnicodeDecodeError: 'locale' codec can't decode byte 0xcd in position 0: decoding error
- threads=16: UnicodeDecodeError: 'locale' codec can't decode byte 0x55 in position 0: decoding error
