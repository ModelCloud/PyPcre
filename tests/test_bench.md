============================= test session starts ==============================
platform linux -- Python 3.13.12, pytest-9.0.2, pluggy-1.6.0
rootdir: /home/work/PyPcre/tests
configfile: pytest.ini
collected 8 items

tests/test_benchmark.py
Engine benchmark: Unicode width sensitivity:

Scenario: 2-byte Unicode text using compiled-pattern findall call

| engine         | calls | total_ms          | best |
|----------------|-------|-------------------|------|
| PyPcre backend | n/a   | n/a               |      |
| regex          | 15000 | 5.383261013776064 | *    |

Scenario: 2-byte Unicode text using compiled-pattern finditer call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 13.751   |      |
| regex          | 15000 | 7.518    | *    |

Scenario: 2-byte Unicode text using compiled-pattern fullmatch call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 7.800    |      |
| regex          | 15000 | 4.659    | *    |

Scenario: 2-byte Unicode text using compiled-pattern match call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 7.708    |      |
| regex          | 15000 | 3.944    | *    |

Scenario: 2-byte Unicode text using compiled-pattern search call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 8.312    |      |
| regex          | 15000 | 4.272    | *    |

Scenario: 3-byte Unicode text using compiled-pattern findall call

| engine         | calls | total_ms          | best |
|----------------|-------|-------------------|------|
| PyPcre backend | n/a   | n/a               |      |
| regex          | 15000 | 5.180360982194543 | *    |

Scenario: 3-byte Unicode text using compiled-pattern finditer call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 14.991   |      |
| regex          | 15000 | 7.643    | *    |

Scenario: 3-byte Unicode text using compiled-pattern fullmatch call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 8.221    |      |
| regex          | 15000 | 4.514    | *    |

Scenario: 3-byte Unicode text using compiled-pattern match call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 8.014    |      |
| regex          | 15000 | 3.971    | *    |

Scenario: 3-byte Unicode text using compiled-pattern search call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 8.012    |      |
| regex          | 15000 | 4.256    | *    |

Scenario: 4-byte Unicode text using compiled-pattern findall call

| engine         | calls | total_ms          | best |
|----------------|-------|-------------------|------|
| PyPcre backend | n/a   | n/a               |      |
| regex          | 15000 | 5.072872969321907 | *    |

Scenario: 4-byte Unicode text using compiled-pattern finditer call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 15.293   |      |
| regex          | 15000 | 8.104    | *    |

Scenario: 4-byte Unicode text using compiled-pattern fullmatch call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 8.494    |      |
| regex          | 15000 | 4.003    | *    |

Scenario: 4-byte Unicode text using compiled-pattern match call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 8.313    |      |
| regex          | 15000 | 4.228    | *    |

Scenario: 4-byte Unicode text using compiled-pattern search call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 7.744    |      |
| regex          | 15000 | 5.079    | *    |

Scenario: ASCII text using compiled-pattern findall call

| engine         | calls | total_ms          | best |
|----------------|-------|-------------------|------|
| PyPcre backend | n/a   | n/a               |      |
| regex          | 15000 | 4.901918000541627 | *    |

Scenario: ASCII text using compiled-pattern finditer call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 6.094    | *    |
| regex          | 15000 | 8.261    |      |

Scenario: ASCII text using compiled-pattern fullmatch call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 3.159    | *    |
| regex          | 15000 | 4.415    |      |

Scenario: ASCII text using compiled-pattern match call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.794    | *    |
| regex          | 15000 | 4.304    |      |

Scenario: ASCII text using compiled-pattern search call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 3.004    | *    |
| regex          | 15000 | 4.468    |      |

Scenario: Latin-1 text using compiled-pattern findall call

| engine         | calls | total_ms          | best |
|----------------|-------|-------------------|------|
| PyPcre backend | n/a   | n/a               |      |
| regex          | 15000 | 5.100919050164521 | *    |

Scenario: Latin-1 text using compiled-pattern finditer call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 14.144   |      |
| regex          | 15000 | 8.206    | *    |

Scenario: Latin-1 text using compiled-pattern fullmatch call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 8.693    |      |
| regex          | 15000 | 4.653    | *    |

Scenario: Latin-1 text using compiled-pattern match call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 8.730    |      |
| regex          | 15000 | 3.912    | *    |

Scenario: Latin-1 text using compiled-pattern search call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 7.535    |      |
| regex          | 15000 | 4.464    | *    |

* best total_ms

API benchmark: Unicode width compiled match:

Scenario: 2-byte Unicode text using compiled-pattern findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 30.106   |      |
| regex  | 15000 | 5.383    | *    |

Scenario: 2-byte Unicode text using compiled-pattern finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 24.660   |      |
| regex  | 15000 | 7.518    | *    |

Scenario: 2-byte Unicode text using compiled-pattern fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.649   |      |
| regex  | 15000 | 4.659    | *    |

Scenario: 2-byte Unicode text using compiled-pattern match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.004   |      |
| regex  | 15000 | 3.944    | *    |

Scenario: 2-byte Unicode text using compiled-pattern search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 16.870   |      |
| regex  | 15000 | 4.272    | *    |

Scenario: 3-byte Unicode text using compiled-pattern findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 32.687   |      |
| regex  | 15000 | 5.180    | *    |

Scenario: 3-byte Unicode text using compiled-pattern finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 25.680   |      |
| regex  | 15000 | 7.643    | *    |

Scenario: 3-byte Unicode text using compiled-pattern fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.188   |      |
| regex  | 15000 | 4.514    | *    |

Scenario: 3-byte Unicode text using compiled-pattern match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.727   |      |
| regex  | 15000 | 3.971    | *    |

Scenario: 3-byte Unicode text using compiled-pattern search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.112   |      |
| regex  | 15000 | 4.256    | *    |

Scenario: 4-byte Unicode text using compiled-pattern findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 33.660   |      |
| regex  | 15000 | 5.073    | *    |

Scenario: 4-byte Unicode text using compiled-pattern finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 26.306   |      |
| regex  | 15000 | 8.104    | *    |

Scenario: 4-byte Unicode text using compiled-pattern fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.343   |      |
| regex  | 15000 | 4.003    | *    |

Scenario: 4-byte Unicode text using compiled-pattern match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.461   |      |
| regex  | 15000 | 4.228    | *    |

Scenario: 4-byte Unicode text using compiled-pattern search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.579   |      |
| regex  | 15000 | 5.079    | *    |

Scenario: ASCII text using compiled-pattern findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.544   |      |
| regex  | 15000 | 4.902    | *    |

Scenario: ASCII text using compiled-pattern finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 16.774   |      |
| regex  | 15000 | 8.261    | *    |

Scenario: ASCII text using compiled-pattern fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 12.524   |      |
| regex  | 15000 | 4.415    | *    |

Scenario: ASCII text using compiled-pattern match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 12.273   |      |
| regex  | 15000 | 4.304    | *    |

Scenario: ASCII text using compiled-pattern search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 12.512   |      |
| regex  | 15000 | 4.468    | *    |

Scenario: Latin-1 text using compiled-pattern findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 28.809   |      |
| regex  | 15000 | 5.101    | *    |

Scenario: Latin-1 text using compiled-pattern finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 24.525   |      |
| regex  | 15000 | 8.206    | *    |

Scenario: Latin-1 text using compiled-pattern fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.105   |      |
| regex  | 15000 | 4.653    | *    |

Scenario: Latin-1 text using compiled-pattern match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.184   |      |
| regex  | 15000 | 3.912    | *    |

Scenario: Latin-1 text using compiled-pattern search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 16.903   |      |
| regex  | 15000 | 4.464    | *    |

* best total_ms

API benchmark: Unicode width module-level match:

Scenario: 2-byte Unicode text using module-level findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 41.826   |      |
| regex  | 15000 | 41.341   | *    |

Scenario: 2-byte Unicode text using module-level finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 35.866   | *    |
| regex  | 15000 | 47.274   |      |

Scenario: 2-byte Unicode text using module-level fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 26.570   | *    |
| regex  | 15000 | 41.050   |      |

Scenario: 2-byte Unicode text using module-level match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 26.284   | *    |
| regex  | 15000 | 41.280   |      |

Scenario: 2-byte Unicode text using module-level search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 24.945   | *    |
| regex  | 15000 | 42.619   |      |

Scenario: 3-byte Unicode text using module-level findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 42.693   | *    |
| regex  | 15000 | 43.015   |      |

Scenario: 3-byte Unicode text using module-level finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 36.562   | *    |
| regex  | 15000 | 49.309   |      |

Scenario: 3-byte Unicode text using module-level fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 26.049   | *    |
| regex  | 15000 | 40.791   |      |

Scenario: 3-byte Unicode text using module-level match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 25.376   | *    |
| regex  | 15000 | 42.452   |      |

Scenario: 3-byte Unicode text using module-level search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 25.939   | *    |
| regex  | 15000 | 43.381   |      |

Scenario: 4-byte Unicode text using module-level findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 44.777   |      |
| regex  | 15000 | 43.286   | *    |

Scenario: 4-byte Unicode text using module-level finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 38.147   | *    |
| regex  | 15000 | 47.740   |      |

Scenario: 4-byte Unicode text using module-level fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 25.780   | *    |
| regex  | 15000 | 43.859   |      |

Scenario: 4-byte Unicode text using module-level match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 25.005   | *    |
| regex  | 15000 | 41.681   |      |

Scenario: 4-byte Unicode text using module-level search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 25.860   | *    |
| regex  | 15000 | 41.891   |      |

Scenario: ASCII text using module-level findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 28.049   | *    |
| regex  | 15000 | 42.487   |      |

Scenario: ASCII text using module-level finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 26.373   | *    |
| regex  | 15000 | 45.664   |      |

Scenario: ASCII text using module-level fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 20.221   | *    |
| regex  | 15000 | 39.734   |      |

Scenario: ASCII text using module-level match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 20.221   | *    |
| regex  | 15000 | 41.195   |      |

Scenario: ASCII text using module-level search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 21.019   | *    |
| regex  | 15000 | 40.465   |      |

Scenario: Latin-1 text using module-level findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 41.200   | *    |
| regex  | 15000 | 42.250   |      |

Scenario: Latin-1 text using module-level finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 34.993   | *    |
| regex  | 15000 | 46.218   |      |

Scenario: Latin-1 text using module-level fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 25.647   | *    |
| regex  | 15000 | 41.590   |      |

Scenario: Latin-1 text using module-level match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 25.196   | *    |
| regex  | 15000 | 40.794   |      |

Scenario: Latin-1 text using module-level search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 26.155   | *    |
| regex  | 15000 | 41.119   |      |

* best total_ms

Engine benchmark: bytes pattern compile:

Scenario: Bytes pattern compile for template b'([A-Za-z]{1,8}){n}'

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 1000  | 5.037    | *    |
| regex          | 1000  | 87.601   |      |

Scenario: Bytes pattern compile for template b'\\x00[\\xff\\xfe]{1,{n}}\\x01'
| engine | calls | total_ms | best |
|----------------|---------|------------|--------|
| PyPcre backend | 1000 | 4.110 | * |
| regex | 1000 | 61.611 | |

Scenario: Bytes pattern compile for template b'foo{n}'
| engine | calls | total_ms | best |
|----------------|---------|------------|--------|
| PyPcre backend | 1000 | 3.707 | * |
| regex | 1000 | 32.290 | |

* best total_ms

API benchmark: bytes pattern compile:

Scenario: Bytes pattern compile for template b'([A-Za-z]{1,8}){n}'

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 1000  | 6.472    | *    |
| regex  | 1000  | 87.601   |      |

Scenario: Bytes pattern compile for template b'\\x00[\\xff\\xfe]{1,{n}}\\x01'

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 1000  | 5.176    | *    |
| regex  | 1000  | 61.611   |      |

Scenario: Bytes pattern compile for template b'foo{n}'

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 1000  | 5.534    | *    |
| regex  | 1000  | 32.290   |      |

* best total_ms
  .
  Engine benchmark: text pattern compile:

Scenario: Text pattern compile for template (?:(?<=foo{n})bar|baz{n})(?!qux)

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 1000  | 6.745    | *    |
| regex          | 1000  | 107.759  |      |

Scenario: Text pattern compile for template (?P<word>[A-Za-z]{{1,8}}){n}

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 1000  | 5.316    | *    |
| regex          | 1000  | 83.193   |      |

Scenario: Text pattern compile for template foo{n}
| engine | calls | total_ms | best |
|----------------|---------|------------|--------|
| PyPcre backend | 1000 | 4.028 | * |
| regex | 1000 | 30.818 | |

* best total_ms

API benchmark: text pattern compile:

Scenario: Text pattern compile for template (?:(?<=foo{n})bar|baz{n})(?!qux)

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 1000  | 8.119    | *    |
| regex  | 1000  | 107.759  |      |

Scenario: Text pattern compile for template (?P<word>[A-Za-z]{{1,8}}){n}

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 1000  | 6.137    | *    |
| regex  | 1000  | 83.193   |      |

Scenario: Text pattern compile for template foo{n}
| engine | calls | total_ms | best |
|----------|---------|------------|--------|
| PyPcre | 1000 | 6.225 | * |
| regex | 1000 | 30.818 | |

* best total_ms
  .
  Engine benchmark: multi-thread text search:

Scenario: Text pattern \bfoo\b using compiled-pattern search call

| engine         | threads | min_ms | median_ms | max_ms | mean_ms | total_ms | best |
|----------------|---------|--------|-----------|--------|---------|----------|------|
| PyPcre backend | 16      | 0.015  | 0.066     | 0.133  | 0.063   | 9.482    | *    |
| regex          | 16      | 0.042  | 8.229     | 21.030 | 9.354   | 22.397   |      |

* best total_ms

API benchmark: multi-thread text search:

Scenario: Text pattern \bfoo\b using compiled-pattern search call

| engine | threads | min_ms | median_ms | max_ms | mean_ms | total_ms | best |
|--------|---------|--------|-----------|--------|---------|----------|------|
| PyPcre | 16      | 0.110  | 0.479     | 0.516  | 0.433   | 10.815   | *    |
| regex  | 16      | 0.042  | 8.229     | 21.030 | 9.354   | 22.397   |      |

* best total_ms
  .
  Engine benchmark: single-thread bytes compiled match:

Scenario: Bytes pattern b'([A-Za-z]+)' using compiled-pattern findall call

| engine         | calls | total_ms           | best |
|----------------|-------|--------------------|------|
| PyPcre backend | n/a   | n/a                |      |
| regex          | 15000 | 11.518146027810872 | *    |

Scenario: Bytes pattern b'([A-Za-z]+)' using compiled-pattern finditer call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 6.282    | *    |
| regex          | 15000 | 14.138   |      |

Scenario: Bytes pattern b'([A-Za-z]+)' using compiled-pattern fullmatch call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.412    | *    |
| regex          | 15000 | 6.703    |      |

Scenario: Bytes pattern b'([A-Za-z]+)' using compiled-pattern match call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.551    | *    |
| regex          | 15000 | 6.621    |      |

Scenario: Bytes pattern b'([A-Za-z]+)' using compiled-pattern search call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.090    | *    |
| regex          | 15000 | 6.816    |      |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using compiled-pattern findall call

| engine         | calls | total_ms          | best |
|----------------|-------|-------------------|------|
| PyPcre backend | n/a   | n/a               |      |
| regex          | 15000 | 5.447261966764927 | *    |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using compiled-pattern finditer call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 4.716    | *    |
| regex          | 15000 | 8.248    |      |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using compiled-pattern fullmatch call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.025    | *    |
| regex          | 15000 | 4.292    |      |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using compiled-pattern match call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.209    | *    |
| regex          | 15000 | 4.296    |      |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using compiled-pattern search call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.057    | *    |
| regex          | 15000 | 5.141    |      |

Scenario: Bytes pattern b'foo' using compiled-pattern findall call

| engine         | calls | total_ms          | best |
|----------------|-------|-------------------|------|
| PyPcre backend | n/a   | n/a               |      |
| regex          | 15000 | 6.437993026338518 | *    |

Scenario: Bytes pattern b'foo' using compiled-pattern finditer call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 4.962    | *    |
| regex          | 15000 | 8.493    |      |

Scenario: Bytes pattern b'foo' using compiled-pattern fullmatch call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 1.691    | *    |
| regex          | 15000 | 4.349    |      |

Scenario: Bytes pattern b'foo' using compiled-pattern match call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 1.318    | *    |
| regex          | 15000 | 4.016    |      |

Scenario: Bytes pattern b'foo' using compiled-pattern search call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 1.743    | *    |
| regex          | 15000 | 4.097    |      |

* best total_ms

API benchmark: single-thread bytes compiled match:

Scenario: Bytes pattern b'([A-Za-z]+)' using compiled-pattern findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 23.020   |      |
| regex  | 15000 | 11.518   | *    |

Scenario: Bytes pattern b'([A-Za-z]+)' using compiled-pattern finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 21.230   |      |
| regex  | 15000 | 14.138   | *    |

Scenario: Bytes pattern b'([A-Za-z]+)' using compiled-pattern fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 11.975   |      |
| regex  | 15000 | 6.703    | *    |

Scenario: Bytes pattern b'([A-Za-z]+)' using compiled-pattern match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 11.932   |      |
| regex  | 15000 | 6.621    | *    |

Scenario: Bytes pattern b'([A-Za-z]+)' using compiled-pattern search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 10.897   |      |
| regex  | 15000 | 6.816    | *    |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using compiled-pattern findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 14.945   |      |
| regex  | 15000 | 5.447    | *    |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using compiled-pattern finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 14.411   |      |
| regex  | 15000 | 8.248    | *    |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using compiled-pattern fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 9.920    |      |
| regex  | 15000 | 4.292    | *    |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using compiled-pattern match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 9.667    |      |
| regex  | 15000 | 4.296    | *    |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using compiled-pattern search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 10.505   |      |
| regex  | 15000 | 5.141    | *    |

Scenario: Bytes pattern b'foo' using compiled-pattern findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 15.719   |      |
| regex  | 15000 | 6.438    | *    |

Scenario: Bytes pattern b'foo' using compiled-pattern finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 14.244   |      |
| regex  | 15000 | 8.493    | *    |

Scenario: Bytes pattern b'foo' using compiled-pattern fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 7.313    |      |
| regex  | 15000 | 4.349    | *    |

Scenario: Bytes pattern b'foo' using compiled-pattern match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 12.380   |      |
| regex  | 15000 | 4.016    | *    |

Scenario: Bytes pattern b'foo' using compiled-pattern search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 9.064    |      |
| regex  | 15000 | 4.097    | *    |

* best total_ms

API benchmark: single-thread bytes module-level match:

Scenario: Bytes pattern b'([A-Za-z]+)' using module-level findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 33.085   | *    |
| regex  | 15000 | 47.494   |      |

Scenario: Bytes pattern b'([A-Za-z]+)' using module-level finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 31.002   | *    |
| regex  | 15000 | 53.592   |      |

Scenario: Bytes pattern b'([A-Za-z]+)' using module-level fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 20.063   | *    |
| regex  | 15000 | 42.728   |      |

Scenario: Bytes pattern b'([A-Za-z]+)' using module-level match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 19.819   | *    |
| regex  | 15000 | 43.163   |      |

Scenario: Bytes pattern b'([A-Za-z]+)' using module-level search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 19.854   | *    |
| regex  | 15000 | 43.427   |      |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using module-level findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 23.391   | *    |
| regex  | 15000 | 41.199   |      |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using module-level finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 23.905   | *    |
| regex  | 15000 | 46.771   |      |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using module-level fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.672   | *    |
| regex  | 15000 | 39.724   |      |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using module-level match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 18.595   | *    |
| regex  | 15000 | 40.091   |      |

Scenario: Bytes pattern b'\\x00[\\xff\\xfe]+\\x01' using module-level search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 18.010   | *    |
| regex  | 15000 | 40.735   |      |

Scenario: Bytes pattern b'foo' using module-level findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 26.068   | *    |
| regex  | 15000 | 42.790   |      |

Scenario: Bytes pattern b'foo' using module-level finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 23.448   | *    |
| regex  | 15000 | 47.357   |      |

Scenario: Bytes pattern b'foo' using module-level fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 16.308   | *    |
| regex  | 15000 | 40.248   |      |

Scenario: Bytes pattern b'foo' using module-level match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 15.331   | *    |
| regex  | 15000 | 40.193   |      |

Scenario: Bytes pattern b'foo' using module-level search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 17.507   | *    |
| regex  | 15000 | 41.529   |      |

* best total_ms
  .
  Engine benchmark: single-thread text compiled match:

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using compiled-pattern findall call

| engine         | calls | total_ms          | best |
|----------------|-------|-------------------|------|
| PyPcre backend | n/a   | n/a               |      |
| regex          | 15000 | 7.960524992085993 | *    |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using compiled-pattern finditer call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 5.337    | *    |
| regex          | 15000 | 10.262   |      |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using compiled-pattern fullmatch call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 0.911    | *    |
| regex          | 15000 | 4.530    |      |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using compiled-pattern match call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 1.121    | *    |
| regex          | 15000 | 3.974    |      |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using compiled-pattern search call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.022    | *    |
| regex          | 15000 | 6.306    |      |

Scenario: Text pattern (?P<word>[A-Za-z]+) using compiled-pattern findall call

| engine         | calls | total_ms           | best |
|----------------|-------|--------------------|------|
| PyPcre backend | n/a   | n/a                |      |
| regex          | 15000 | 11.614670976996422 | *    |

Scenario: Text pattern (?P<word>[A-Za-z]+) using compiled-pattern finditer call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 5.245    | *    |
| regex          | 15000 | 14.879   |      |

Scenario: Text pattern (?P<word>[A-Za-z]+) using compiled-pattern fullmatch call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.292    | *    |
| regex          | 15000 | 6.516    |      |

Scenario: Text pattern (?P<word>[A-Za-z]+) using compiled-pattern match call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.754    | *    |
| regex          | 15000 | 6.082    |      |

Scenario: Text pattern (?P<word>[A-Za-z]+) using compiled-pattern search call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.373    | *    |
| regex          | 15000 | 7.306    |      |

Scenario: Text pattern \bfoo\b using compiled-pattern findall call

| engine         | calls | total_ms          | best |
|----------------|-------|-------------------|------|
| PyPcre backend | n/a   | n/a               |      |
| regex          | 15000 | 6.172031979076564 | *    |

Scenario: Text pattern \bfoo\b using compiled-pattern finditer call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 5.193    | *    |
| regex          | 15000 | 9.335    |      |

Scenario: Text pattern \bfoo\b using compiled-pattern fullmatch call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 1.716    | *    |
| regex          | 15000 | 4.065    |      |

Scenario: Text pattern \bfoo\b using compiled-pattern match call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 1.555    | *    |
| regex          | 15000 | 3.756    |      |

Scenario: Text pattern \bfoo\b using compiled-pattern search call

| engine         | calls | total_ms | best |
|----------------|-------|----------|------|
| PyPcre backend | 15000 | 2.162    | *    |
| regex          | 15000 | 4.837    |      |

* best total_ms

API benchmark: single-thread text compiled match:

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using compiled-pattern findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 15.031   |      |
| regex  | 15000 | 7.961    | *    |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using compiled-pattern finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 13.813   |      |
| regex  | 15000 | 10.262   | *    |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using compiled-pattern fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 6.290    |      |
| regex  | 15000 | 4.530    | *    |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using compiled-pattern match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 5.777    |      |
| regex  | 15000 | 3.974    | *    |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using compiled-pattern search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 10.529   |      |
| regex  | 15000 | 6.306    | *    |

Scenario: Text pattern (?P<word>[A-Za-z]+) using compiled-pattern findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 23.975   |      |
| regex  | 15000 | 11.615   | *    |

Scenario: Text pattern (?P<word>[A-Za-z]+) using compiled-pattern finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 21.445   |      |
| regex  | 15000 | 14.879   | *    |

Scenario: Text pattern (?P<word>[A-Za-z]+) using compiled-pattern fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 12.434   |      |
| regex  | 15000 | 6.516    | *    |

Scenario: Text pattern (?P<word>[A-Za-z]+) using compiled-pattern match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 11.989   |      |
| regex  | 15000 | 6.082    | *    |

Scenario: Text pattern (?P<word>[A-Za-z]+) using compiled-pattern search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 11.985   |      |
| regex  | 15000 | 7.306    | *    |

Scenario: Text pattern \bfoo\b using compiled-pattern findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 18.043   |      |
| regex  | 15000 | 6.172    | *    |

Scenario: Text pattern \bfoo\b using compiled-pattern finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 16.410   |      |
| regex  | 15000 | 9.335    | *    |

Scenario: Text pattern \bfoo\b using compiled-pattern fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 8.680    |      |
| regex  | 15000 | 4.065    | *    |

Scenario: Text pattern \bfoo\b using compiled-pattern match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 8.923    |      |
| regex  | 15000 | 3.756    | *    |

Scenario: Text pattern \bfoo\b using compiled-pattern search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 9.638    |      |
| regex  | 15000 | 4.837    | *    |

* best total_ms

API benchmark: single-thread text module-level match:

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using module-level findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 24.734   | *    |
| regex  | 15000 | 43.882   |      |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using module-level finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 23.602   | *    |
| regex  | 15000 | 49.837   |      |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using module-level fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 14.018   | *    |
| regex  | 15000 | 40.378   |      |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using module-level match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 14.322   | *    |
| regex  | 15000 | 39.292   |      |

Scenario: Text pattern (?:(?<=foo)bar|baz)(?!qux) using module-level search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 18.534   | *    |
| regex  | 15000 | 43.662   |      |

Scenario: Text pattern (?P<word>[A-Za-z]+) using module-level findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 34.155   | *    |
| regex  | 15000 | 48.833   |      |

Scenario: Text pattern (?P<word>[A-Za-z]+) using module-level finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 32.557   | *    |
| regex  | 15000 | 54.193   |      |

Scenario: Text pattern (?P<word>[A-Za-z]+) using module-level fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 19.818   | *    |
| regex  | 15000 | 44.650   |      |

Scenario: Text pattern (?P<word>[A-Za-z]+) using module-level match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 21.285   | *    |
| regex  | 15000 | 43.413   |      |

Scenario: Text pattern (?P<word>[A-Za-z]+) using module-level search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 20.841   | *    |
| regex  | 15000 | 42.907   |      |

Scenario: Text pattern \bfoo\b using module-level findall call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 28.673   | *    |
| regex  | 15000 | 42.862   |      |

Scenario: Text pattern \bfoo\b using module-level finditer call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 26.610   | *    |
| regex  | 15000 | 47.473   |      |

Scenario: Text pattern \bfoo\b using module-level fullmatch call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 16.653   | *    |
| regex  | 15000 | 39.465   |      |

Scenario: Text pattern \bfoo\b using module-level match call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 16.253   | *    |
| regex  | 15000 | 39.028   |      |

Scenario: Text pattern \bfoo\b using module-level search call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 15000 | 18.095   | *    |
| regex  | 15000 | 40.604   |      |

* best total_ms
  .
  API benchmark: bytes transform:

Scenario: Bytes split CSV | compiled-pattern split call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 9000  | 18.825   |      |
| regex  | 9000  | 6.956    | *    |

Scenario: Bytes split CSV | module-level split call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 9000  | 25.781   | *    |
| regex  | 9000  | 29.967   |      |

Scenario: Bytes substitute digits | compiled-pattern sub call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 9000  | 48.840   |      |
| regex  | 9000  | 7.032    | *    |

Scenario: Bytes substitute digits | module-level sub call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 9000  | 52.910   |      |
| regex  | 9000  | 31.747   | *    |

* best total_ms
  .
  API benchmark: text transform:

Scenario: Text split CSV | compiled-pattern split call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 9000  | 18.976   |      |
| regex  | 9000  | 5.756    | *    |

Scenario: Text split CSV | module-level split call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 9000  | 25.076   | *    |
| regex  | 9000  | 30.925   |      |

Scenario: Text substitute digits | compiled-pattern sub call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 9000  | 42.595   |      |
| regex  | 9000  | 8.314    | *    |

Scenario: Text substitute digits | module-level sub call

| engine | calls | total_ms | best |
|--------|-------|----------|------|
| PyPcre | 9000  | 52.068   |      |
| regex  | 9000  | 32.533   | *    |

* best total_ms

=============================== warnings summary ===============================
pcre/pcre.py:15
/home/work/PyPcre/pcre/pcre.py:15: DeprecationWarning: module 'sre_parse' is deprecated
import sre_parse as _parser

-- Docs: https://docs.pytest.org/en/stable/how-to/capture-warnings.html
========================= 8 passed, 1 warning in 6.52s =========================
