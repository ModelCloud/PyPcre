# PyPcre vs regex benchmark

- Generated from `test_benchmark_pypcre_vs_regex.py`
- Python runtime: `3.13.12` free-threaded=`true`
- APIs: `match`, `fullmatch`, `compile`
- Iterations per thread: `1000`
- Benchmark rounds per scenario: `5`
- Thread counts: `1, 2, 4, 8, 16`
- Timing excludes thread startup by synchronizing all worker threads before the measured region.
- `pypcre (ms)` / `regex (ms)` are each-round averages: wall-clock elapsed time per round, with the highest and lowest rounds removed before averaging.
- `regex.compile` failures under free-threaded concurrency are recorded as `error` instead of failing the whole benchmark.
- `pypcre_vs_regex` shows `faster xx.x%`, `slower xx.x%`, `same`, or `n/a` based on wall-clock time.

## match (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 3.851       | 0.602      | slower 539.6%   |
| 2       | 4.422       | 1.345      | slower 228.9%   |
| 4       | 5.257       | 1.754      | slower 199.7%   |
| 8       | 4.966       | 11.726     | faster 57.7%    |
| 16      | 7.319       | 20.155     | faster 63.7%    |

## fullmatch (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 2.059       | 0.347      | slower 493.3%   |
| 2       | 1.554       | 0.626      | slower 148.2%   |
| 4       | 2.148       | 1.664      | slower 29.1%    |
| 8       | 3.748       | 8.107      | faster 53.8%    |
| 16      | 7.842       | 17.415     | faster 55.0%    |

## compile (1000 times avg)

| threads | pypcre (ms) | regex (ms) | pypcre_vs_regex |
|---------|-------------|------------|-----------------|
| 1       | 6.801       | 139.383    | faster 95.1%    |
| 2       | 16.253      | error      | n/a             |
| 4       | 16.421      | error      | n/a             |
| 8       | 33.537      | error      | n/a             |
| 16      | 38.150      | error      | n/a             |

regex errors:

- threads=2: UnicodeDecodeError: 'locale' codec can't decode byte 0xc8 in position 1: decoding error
- threads=4: UnicodeDecodeError: 'locale' codec can't decode byte 0xc8 in position 1: decoding error
- threads=8: UnicodeDecodeError: 'locale' codec can't decode byte 0xc8 in position 1: decoding error
- threads=16: RuntimeError: dictionary changed size during iteration
