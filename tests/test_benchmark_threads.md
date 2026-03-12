Benchmark: multi-thread text search:

Scenario: Text pattern \bfoo\b using compiled-pattern search call

Threads: 1

| engine         | threads | calls_per_thread | total_calls | min_ms | median_ms | max_ms | mean_ms | total_ms | diff_pct | best |
|----------------|---------|------------------|-------------|--------|-----------|--------|---------|----------|----------|------|
| PyPcre         | 1       | 120              | 120         | 0.737  | 0.737     | 0.737  | 0.737   | 0.935    | +1268.0% |      |
| PyPcre backend | 1       | 120              | 120         | 0.063  | 0.063     | 0.063  | 0.063   | 0.068    | +0.0%    | *    |
| regex          | 1       | 120              | 120         | 0.069  | 0.069     | 0.069  | 0.069   | 0.106    | +55.6%   |      |

Threads: 2

| engine         | threads | calls_per_thread | total_calls | min_ms | median_ms | max_ms | mean_ms | total_ms | diff_pct | best |
|----------------|---------|------------------|-------------|--------|-----------|--------|---------|----------|----------|------|
| PyPcre         | 2       | 120              | 240         | 1.354  | 1.431     | 1.507  | 1.431   | 1.966    | +844.1%  |      |
| PyPcre backend | 2       | 120              | 240         | 0.062  | 0.067     | 0.072  | 0.067   | 0.390    | +87.2%   |      |
| regex          | 2       | 120              | 240         | 0.050  | 0.051     | 0.052  | 0.051   | 0.208    | +0.0%    | *    |

Threads: 4

| engine         | threads | calls_per_thread | total_calls | min_ms | median_ms | max_ms | mean_ms | total_ms | diff_pct | best |
|----------------|---------|------------------|-------------|--------|-----------|--------|---------|----------|----------|------|
| PyPcre         | 4       | 120              | 480         | 2.131  | 2.583     | 2.860  | 2.539   | 3.322    | +794.6%  |      |
| PyPcre backend | 4       | 120              | 480         | 0.072  | 0.157     | 0.190  | 0.144   | 0.654    | +76.1%   |      |
| regex          | 4       | 120              | 480         | 0.097  | 0.162     | 0.204  | 0.156   | 0.371    | +0.0%    | *    |

Threads: 8

| engine         | threads | calls_per_thread | total_calls | min_ms | median_ms | max_ms | mean_ms | total_ms | diff_pct | best |
|----------------|---------|------------------|-------------|--------|-----------|--------|---------|----------|----------|------|
| PyPcre         | 8       | 120              | 960         | 2.727  | 3.401     | 4.143  | 3.404   | 4.516    | +402.4%  |      |
| PyPcre backend | 8       | 120              | 960         | 0.791  | 1.078     | 1.174  | 1.036   | 2.083    | +131.7%  |      |
| regex          | 8       | 120              | 960         | 0.049  | 0.260     | 0.617  | 0.316   | 0.899    | +0.0%    | *    |

* best = lowest total_ms; diff_pct = percentage slower than best
