<!--
# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium
-->
<div align=center>
<img width="500" alt="image" src="https://github.com/user-attachments/assets/92964c3a-f82e-4949-bd27-278f57c62d9f" />
</div>
<h1 align="center">PyPcre (Python PCRE2 Binding) 🧬</h1>

<p align=center>
Fast, free-threaded Python bindings for `PCRE2` with a stable `stdlib.re`-compatible API. ⚡
</p>

<p align="center">
    <a href="https://github.com/ModelCloud/PyPcre/releases" style="text-decoration:none;"><img alt="GitHub release" src="https://img.shields.io/github/release/ModelCloud/PyPcre.svg"></a>
    <a href="https://pypi.org/project/PyPcre/" style="text-decoration:none;"><img alt="PyPI - Version" src="https://img.shields.io/pypi/v/PyPcre"></a>
    <a href="https://pepy.tech/projects/PyPcre" style="text-decoration:none;"><img src="https://static.pepy.tech/badge/PyPcre" alt="PyPI Downloads"></a>
    <a href="https://github.com/ModelCloud/PyPcre/blob/main/LICENSE"><img src="https://img.shields.io/pypi/l/PyPcre"></a>
    <a href="https://huggingface.co/modelcloud/"><img src="https://img.shields.io/badge/🤗%20Hugging%20Face-ModelCloud-%23ff8811.svg"></a>
</p>



## Latest News 🚀
* 08/25/2026 **[0.6.2](https://github.com/ModelCloud/PyPcre/releases/tag/v0.6.2)**: Hardened packaging metadata and fixed FreeBSD/WSL CI failures on PCRE2 10.42 runtimes, including legacy replacement-template and JIT-fallback compatibility. 🛠️🐧🪟
* 08/25/2026 **[0.6.1](https://github.com/ModelCloud/PyPcre/releases/tag/v0.6.1)**: Workaround for PCRE2 10.46/10.47 JIT start-optimization regressions, new verifying clobber suite, second memory/thread-safety sweep, Python 3.15 `sre_parse` removal compatibility, aligned `Pattern` `pos`/`endpos` signatures, and `setuptools` metadata fixes. 🛡️🧵🐍
* 08/09/2026 **[0.6.0](https://github.com/ModelCloud/PyPcre/releases/tag/v0.6.0)**: `findall`, `finditer`, `sub`/`subn`, `split`, and `match`/`search`/`fullmatch` hot-path speedups up to **46x** vs `stdlib.re` and **48x** vs `regex`, with free-threaded `findall` reaching **13.8x** on 8 threads. 🚀⚡
* 07/27/2026 [0.5.0](https://github.com/ModelCloud/PyPcre/releases/tag/v0.5.0): Zero-copy buffer-protocol subject support (`mmap.mmap`, `bytearray`, `array.array`) with UTF-8 validation and GIL=0-safe memory pinning. 🗂️⚡
* 07/24/2026 [0.4.0](https://github.com/ModelCloud/PyPcre/releases/tag/v0.4.0): C extension hardening (memory/pointer safety, bounds checks, atomic allocator init), GIL=0 safety verified, vectorized UTF-8 index/offset conversion, GIL-release threshold for small calls, C `findall` implementation, and README competitor benchmarks. 🛡️⚡
* 04/13/2026 [0.3.0](https://github.com/ModelCloud/PyPcre/releases/tag/v0.3.0): Lower-overhead public `Match` objects, faster hot-path `match()` / `search()` / `fullmatch()` / `findall()`, and tighter free-threaded execution. ⚡
* 03/22/2026 [0.2.15](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.15): Python 3.15 `re` compatibility (`prefixmatch`, `NOFLAG`) ✅
* 03/21/2026 [0.2.14](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.14): Python 3.14 compatibility 🐍
* 03/02/2026 [0.2.11](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.11): Auto-detect `Visual Studio` in Windows environments during install and compile. 🪟
* 02/24/2026 [0.2.10](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.10): Allow a `Visual Studio` (VS) compiler version check override via an environment variable. 🧰
* 12/15/2025 [0.2.8](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.8): Fixed multi-arch Linux OS compatibility when both x86_64 and i386 `pcre2` libraries are installed. 🐧
* 10/20/2025 [0.2.4](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.4): Removed the dependency on a system `python3-dev` package. `Python.h` will be downloaded optimistically from python.org when needed. 📦
* 10/12/2025 [0.2.3](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.3): 🤗 Full `GIL=0` compliance for Python >= 3.13T. Reduced cache thread contention. Improved performance across all APIs. Expanded CI test coverage. FreeBSD, Solaris, and Windows compatibility validated.
* 10/09/2025 [0.1.0](https://github.com/ModelCloud/PyPcre/releases/tag/v0.1.0): 🎉 First release. Thread-safe, with auto JIT, auto pattern caching, and optimistic linking to the system library for fast installs.

## Why PyPcre ⚡

PyPcre pairs Python's familiar `re`-compatible API with the real `PCRE2` engine. You keep the ergonomics of the standard library while gaining a more capable regex engine, optional JIT, explicit threading support, and a binding designed and tested for free-threaded Python. 🧠⚡

### Big Wins 🏆

- 🧬 **Full power of PCRE2**: PyPcre uses the real `PCRE2` engine, so you get native compile options, semantics, JIT, and upstream tuning.
- 🔥 **More expressive regex syntax**: `PCRE2` supports constructs beyond stdlib `re`, including atomic groups `(?>...)`, possessive quantifiers `++`, branch-reset groups `(?|...)`, richer lookarounds, and backtracking control verbs like `(*SKIP)(*FAIL)`.
- 🧵 **Thread-safe into `nogil`**: PyPcre is built for `PYTHON_GIL=0`, with CI coverage and `parallel_map()` for multi-subject fan-out.
- ⚡ **Fast on real workloads**: `PCRE2` JIT plus cached compiled patterns lets PyPcre match or beat `re` and `regex` on many common scans, especially multiline searches, lookaround-heavy patterns, and free-threaded execution.
- 🛡️ **Safer operational story**: PyPcre prefers the system `libpcre2-8` shared library so normal OS package updates can bring security and bug-fix benefits without a bundled fork.
- ✅ **Validated thoroughly**: the project runs API, fuzz, concurrency, and memory-safety tests.

### Quick Comparison 🥊

| Area | PyPcre | `stdlib.re` | `regex` |
| --- | --- | --- | --- |
| Engine | Full `PCRE2` ✅ | CPython stdlib engine | Separate engine, not `PCRE2` |
| `PCRE2` syntax and flags | Full access ✅ | No | No |
| Syntax power | Very rich ✅ | More limited | Rich, but different from `PCRE2` |
| JIT execution | `PCRE2` JIT ✅ | No | No |
| `re`-compatible API surface | Stable and familiar ✅ | Native | Similar, but not the main goal |
| Free-threaded support | Built and tested for `PYTHON_GIL=0` ✅ | No explicit free-threaded support | No explicit free-threaded fan-out layer |
| Built-in threaded subject fan-out | `parallel_map()` ✅ | No | No |
| System library updates | Uses system `libpcre2-8` by default ✅ | N/A | N/A |

### Benchmark Highlights 🏁

The tables below summarize representative public workloads. Lower is better.

#### Fan-out and API speedups

Both interpreter runs used the same scheduler policy. The host reports 12
performance logical CPUs and 4 efficiency logical CPUs; macOS does not provide
an unprivileged hard per-process CPU mask, so these measurements record the
topology rather than claiming hard CPU affinity.

| Workload | Python 3.10 | Python 3.14t/GIL=0 |
| --- | ---: | ---: |
| `parallel_map(search)`, 16 × 1 MiB subjects, 12 workers | **8.57x** | **7.85x** |
| `parallel_map(findall)`, 48 × 1 MiB subjects, 12 workers | **11.51x** | **11.25x** |
| No-op `escape("literal")` | **5.0x** | **3.6x** |
| No-op `escape(b"literal")` | **6.9x** | **6.5x** |
| Bound literal `sub(..., count=1)` | **4.7x** | **4.3x** |
| Bound numeric-reference `sub(..., count=1)` | **4.3x** | **4.1x** |
| Bound explicit-reference `sub(..., count=1)` | **4.6x** | **4.3x** |
| Bound named-reference `sub(..., count=1)` | **4.2x** | **4.3x** |
| Bound numeric-reference `sub(..., count=2)` | **4.6x** | **4.2x** |
| Bound numeric-reference `sub(..., count=4)` | **6.0x** | **5.1x** |
| Bound numeric-reference `sub(..., count=8)` | **8.6x** | **6.1x** |
| Cached compile with `re.I` | **6.7x** | **6.9x** |
| Cached compile with `re.I|re.M|re.S|re.X` | **6.2x** | **6.4x** |
| First-read `lastindex` cost, sole capture | **11.8x** | **8.2x** |
| Deprecated `template()` compatibility call | **6.2x** | **1.1x** |
| Literal-capture `findall`, 100 matches | **5.0x** | **6.2x** |
| Literal-capture `findall`, 500 matches | **7.0x** | **8.4x** |
| Two literal captures `findall`, 100 matches | **7.5x** | **10.8x** |
| Two literal captures `findall`, 500 matches | **13.5x** | **18.3x** |
| Eight literal captures `findall`, 500 matches | **24.5x** | **29.4x** |
| Three literal captures `split`, 500 captures | **4.5x** | **6.5x** |
| Eight literal captures `split`, 500 captures | **6.6x** | **9.0x** |
| Literal-capture `split`, 100 captures | **2.6x** | **3.5x** |
| Literal-capture `split`, 2,000 captures | **3.1x** | **3.8x** |
| Bound one-character literal `Pattern.split` | **2.1x** | **1.7x** |
| Bound backreference `sub` hot path | **1.38 μs** | **1.14 μs** |
| One-match numeric-reference `Pattern.sub` | **0.45 μs** | **0.34 μs** |
| One-match explicit-reference `Pattern.sub` | **0.45 μs** | **0.31 μs** |
| One-match named-reference `Pattern.sub` | **0.46 μs** | **0.33 μs** |
| Repeated call-local `Match.groups()` | **~0.05 μs** | **~0.05 μs** |
| Call-local `Match.expand(r"[\\1]")` | **0.07 μs** | **0.07 μs** |
| Call-local `Match.expand(r"[\\g<1>]")` | **0.11 μs** | **0.08 μs** |
| Call-local `Match.expand(r"[\\g<word>]")` | **0.13 μs** | **0.11 μs** |
| Call-local two-name `Match.expand` | **0.18 μs** | **0.15 μs** |
| Call-local three-name `Match.expand` | **0.23 μs** | **0.20 μs** |
| Call-local eight-name `Match.expand` | **0.44 μs** | **0.39 μs** |
| Literal-backslash + named `Match.expand` | **0.12 μs** | **0.10 μs** |
| Named + backslash-suffix `Match.expand` | **0.16 μs** | **0.13 μs** |
| Repeated default `compile("(x)")` | **0.49 μs** | **0.38 μs** |
| Repeated integer-flagged `compile("x", CASELESS)` | **1.16 μs** | **0.81 μs** |

The parallel figures are serial-to-parallel speedups and preserve input order
and exception behavior. They were measured with the same scheduler policy for
both interpreters on the Apple host.

#### `findall` — large multiline and lookaround workloads

| Workload | PyPcre (ms) | `re` (ms) | `regex` (ms) | PyPcre edge |
| --- | ---: | ---: | ---: | --- |
| Extract `WARN` / `ERROR` lines (multiline) | `0.471` | `15.098` | `16.281` | **32.1x** vs `re`, **34.6x** vs `regex` |
| Per-line full-name extraction (multiline) | `0.588` | `14.305` | `8.000` | **24.3x** vs `re`, **13.6x** vs `regex` |
| Lookbehind + negative-lookahead tokens | `1.013` | `6.129` | `5.267` | **6.1x** vs `re`, **5.2x** vs `regex` |

Patterns used:

```python
# WARN/ERROR lines and full-name extraction
^(?:WARN|ERROR).*?$        # with re.MULTILINE / pcre.Flag.MULTILINE
^[A-Z][a-z]+ [A-Z][a-z]+   # with re.MULTILINE / pcre.Flag.MULTILINE

# lookbehind + negative lookahead
(?:(?<=foo)bar|baz)(?!qux)
```

#### `finditer` — same workloads

Measured on Python 3.10.11 arm64 with compiled-pattern reuse and JIT enabled. A reproducible version lives in [`benchmarks/finditer_bench.py`](benchmarks/finditer_bench.py).

| Workload | PyPcre (ms) | `re` (ms) | `regex` (ms) | PyPcre edge |
| --- | ---: | ---: | ---: | --- |
| Extract `WARN` / `ERROR` lines | `0.449` | `17.145` | `16.175` | **38.2x** vs `re`, **36.0x** vs `regex` |
| Per-line full-name extraction | `0.564` | `16.258` | `7.874` | **28.8x** vs `re`, **14.0x** vs `regex` |
| Lookbehind + negative lookahead | `1.942` | `8.548` | `6.037` | **4.4x** vs `re`, **3.1x** vs `regex` |

#### `sub` / `subn` — high-volume replacement workloads

Measured on Python 3.10.11 arm64 with compiled-pattern reuse and JIT enabled. Values are medians of three outer runs; lower is better. The benchmark replaces 100,000 space-separated tokens.

A reproducible version lives in [`benchmarks/sub_bench.py`](benchmarks/sub_bench.py).

| Workload | PyPcre (ms) | `re` (ms) | `regex` (ms) | PyPcre edge |
| --- | ---: | ---: | ---: | --- |
| Literal replacement (`\w+` → `[X]`) | `3.127` | `6.489` | `9.992` | **2.1x** vs `re`, **3.2x** vs `regex` |
| Single numeric backref (`(w)\d+` → `[\1]`) | `4.003` | `30.098` | `12.481` | **7.5x** vs `re`, **3.1x** vs `regex` |
| Two numeric backrefs (`(w)(\d+)` → `\2-\1`) | `9.940` | `35.052` | `16.201` | **3.5x** vs `re`, **1.6x** vs `regex` |
| Named backref (`(?P<g>\w+)` → `<\g<g>>`) | `9.765` | `32.434` | `15.216` | **3.3x** vs `re`, **1.6x** vs `regex` |

#### `split` — high-volume delimiter workloads

Measured on Python 3.10.11 arm64 with compiled-pattern reuse and JIT enabled. Values are medians of three outer runs; lower is better. The benchmark splits 100,000 space-separated tokens.

A reproducible version lives in [`benchmarks/split_bench.py`](benchmarks/split_bench.py).

| Workload | PyPcre (ms) | `re` (ms) | `regex` (ms) | PyPcre edge |
| --- | ---: | ---: | ---: | --- |
| Delimiter no group (`\s+`) | `3.349` | `8.013` | `9.913` | **2.4x** vs `re`, **3.0x** vs `regex` |
| Delimiter with group (`(\s+)`) | `5.080` | `9.142` | `12.374` | **1.8x** vs `re`, **2.4x** vs `regex` |
| Single char (` `) | `1.484` | `1.666` | `6.963` | **1.1x** vs `re`, **4.7x** vs `regex` |
| Single char with group (`( )`) | `1.772` | `4.203` | `8.875` | **2.4x** vs `re`, **5.0x** vs `regex` |
| Empty pattern (`''`) | `35.736` | `18.386` | `35.801` | **0.5x** vs `re`, parity vs `regex` |

### Free-Threaded Benchmark Highlights 🧵

Measured on the same Apple arm64 `Python 3.14.0rc2` free-threaded build with `8` threads fanning out over split copies of each workload. Values are medians of three outer runs; lower is better.

A reproducible version lives in [`benchmarks/free_threaded_bench.py`](benchmarks/free_threaded_bench.py).

| Workload | PyPcre (ms) | `re` (ms) | `regex` (ms) | PyPcre edge |
| --- | ---: | ---: | ---: | --- |
| Extract `WARN` / `ERROR` lines (`findall`) | `0.401` | `2.430` | `2.626` | **6.1x** vs `re`, **6.6x** vs `regex` |
| Per-line full-name extraction (`findall`) | `0.462` | `2.351` | `1.477` | **5.1x** vs `re`, **3.2x** vs `regex` |

PyPcre is the stronger all-around choice when you want more than the baseline: full `PCRE2` features, more expressive syntax, JIT, explicit free-threaded support, and a stable `re`-compatible API surface. It keeps Python ergonomics while giving you a substantially more capable engine. 🚀

## Installation 📦

```bash
pip install PyPcre
```

By default, the package links against the system `libpcre2-8` shared library for fast installs and to inherit OS security updates. See [Building](#building) for manual build details.

## Platform Support (Validated) ✅

`Linux`, `macOS`, `Windows`, `WSL`, `FreeBSD`


## Usage 🛠️

If you already use the standard library `re`, migration is often just an import swap:

```python
import pcre as re
```

The high-level API stays close to the standard library, so most existing `re` code can move over with little or no rewriting.

### Quick start 🚀

```python
from pcre import compile, findall, match, search, Flag

if match(r"(?P<word>\\w+)", "hello world"):
    print("found word")

pattern = compile(rb"\d+", flags=Flag.MULTILINE)
numbers = pattern.findall(b"line 1\nline 22")
```

### API Overview 🧭

- Module helpers: `prefixmatch`, `match`, `search`, `fullmatch`, `finditer`,
  `findall`, `split`, `sub`, `subn`, `compile`, `escape`, `purge`,
  `template`, `parallel_map`, `configure`, `configure_threads`,
  `configure_thread_pool`, `shutdown_thread_pool`, `set_cache_limit`,
  `get_cache_limit`, and `clear_cache`.
- `compile()` returns a `Pattern` object with the familiar matching helpers
  plus `split()`, `sub()`, and `subn()`.
- `Pattern` exposes `.pattern`, `.flags`, `.jit`, `.groupindex`, and `.groups`
  for introspection.
- `Match` objects expose the usual `group()`, `groups()`, `groupdict()`,
  `start()`, `end()`, `span()`, and `expand()` methods, along with `.re`,
  `.string`, `.pos`, `.endpos`, `.lastindex`, `.lastgroup`, and `.regs`.
- Flags are available through `pcre.Flag` and familiar aliases such as
  `IGNORECASE`, `MULTILINE`, `DOTALL`, `VERBOSE`, `ASCII`, `UNICODE`, and
  `NOFLAG`.
- Errors are raised as `pcre.PcreError`; `error` and `PatternError` are kept as
  compatibility aliases.

### Common examples 🧪

🧪 Compiled patterns:

```python
from pcre import compile, Flag

pattern = compile(r"(?P<name>[A-Za-z]+)", flags=Flag.CASELESS)
match = pattern.search("User: alice")
print(match.group("name"))  # alice
```

🔁 Substitution:

```python
from pcre import sub

result = sub(r"\d+", "#", "room 101")
print(result)  # room #
```

🗂️ Bytes:

```python
from pcre import compile

pattern = compile(br"\w+")
print(pattern.findall(b"ab cd"))  # [b'ab', b'cd']
```

### Stdlib `re` compatibility 🔁

- Module-level helpers and the `Pattern` class follow the same call shapes as
  the standard library `re` module, including `pos`, `endpos`, and `flags`
  behavior.
- Python 3.15's `prefixmatch()` alias is available at both the module level
  and on compiled `Pattern` objects, and `re.NOFLAG` is re-exported as the
  zero-value compatibility alias.
- `Pattern` mirrors `re.Pattern` attributes like `.pattern`, `.groupindex`,
  and `.groups`, while `Match` objects surface the familiar `.re`, `.string`,
  `.pos`, `.endpos`, `.lastindex`, `.lastgroup`, `.regs`, and `.expand()` API.
- Substitution helpers enforce the same type rules as the standard library
  `re` module: string patterns require string replacements, byte patterns
  require bytes-like replacements, and callable replacements receive the
  wrapped `Match`.
- `compile()` accepts native `Flag` values as well as compatible
  `re.RegexFlag` members from the standard library. Supported stdlib flags
  map 1:1 to PCRE2 options (`IGNORECASE→CASELESS`, `MULTILINE→MULTILINE`,
  `DOTALL→DOTALL`, `VERBOSE→EXTENDED`); passing unsupported stdlib flags
  raises a compatibility `ValueError` to prevent silent divergences.
- `pcre.escape()` delegates directly to `re.escape` for byte and text
  patterns so escaping semantics remain identical.
- String patterns enable Unicode behavior by default. Byte patterns do not.

### `regex` package compatibility 🔄

The [`regex`](https://pypi.org/project/regex/) package interprets
`\uXXXX` and `\UXXXXXXXX` escapes as UTF-8 code points, while PCRE2 expects
hexadecimal escapes to use the `\x{...}` form. Enable `Flag.COMPAT_UNICODE_ESCAPE` to
translate those escapes automatically when compiling patterns:

```python
from pcre import compile, Flag

pattern = compile(r"\\U0001F600", flags=Flag.COMPAT_UNICODE_ESCAPE)
assert pattern.pattern == r"\\x{0001F600}"
```

Set the default behavior globally with `pcre.configure(compat_regex=True)`
so that subsequent calls to `compile()` and the module-level helpers apply
the conversion without repeating the flag.

### Common issues ⚠️

- Unsupported stdlib flags such as `re.DEBUG`, `re.LOCALE`, and `re.ASCII`
  raise `ValueError`. If you want ASCII-style behavior, use `pcre.ASCII` or
  `Flag.NO_UTF | Flag.NO_UCP`.
- Replacement types must match the subject type: text patterns use `str`
  replacements, while byte patterns use bytes-like replacements.
- If you are porting patterns from the third-party `regex` package, check
  `\u` and `\U` escapes first. That is the most common compatibility gap.
- Most users do not need to tune caching, JIT, or threading. The defaults are
  intended to work well out of the box.

### Optional runtime controls 🎛️

- ⚙️ `pcre.configure(jit=False)` disables JIT globally. `Flag.JIT` and
  `Flag.NO_JIT` let you override that per pattern.
- 🧹 `pcre.set_cache_limit()`, `pcre.get_cache_limit()`, and `pcre.clear_cache()`
  control every high-level compile/template helper cache in the active context.
  A zero limit disables them, and `None` uses a 256-entry hard safety ceiling
  rather than permitting unbounded growth. High-level cache entries never cross
  thread scope in the default thread-local strategy; a clear invalidates live
  workers' high-level helper caches on their next cache-backed call. Backend
  scratch buffers remain thread-scoped and are released when that thread exits.
  Oversized patterns and templates are not retained.
- 🧵 `pcre.configure_threads()`, `pcre.configure_thread_pool()`,
  `shutdown_thread_pool()`, `Flag.THREADS`, and `Flag.NO_THREADS` are available
  if you want to opt into or restrict threaded execution.

## Building 🏗️

The extension links against an existing `libpcre2-8` installation. Install the development headers for your platform before building,
for example `apt install libpcre2-dev` on Debian/Ubuntu, `dnf install pcre2-devel`
on Fedora/RHEL derivatives, or `brew install pcre2` on macOS.

If the headers or library live in a non-standard location, you can export one
or more of the following environment variables prior to invoking the build
(`pip install .`, `python -m build`, etc.):

- `PYPCRE_ROOT`
- `PYPCRE_INCLUDE_DIR`
- `PYPCRE_LIBRARY_DIR`
- `PYPCRE_LIBRARY_PATH` *(pathsep-separated directories or explicit library files to
  prioritize when resolving `libpcre2-8`)*
- `PYPCRE_LIBRARIES`
- `PYPCRE_CFLAGS`
- `PYPCRE_LDFLAGS`

If you would rather force a source build, set `PYPCRE_BUILD_FROM_SOURCE=1`
before installing.

When `pkg-config` is available, the build automatically picks up the
required include and link flags via `pkg-config --cflags/--libs libpcre2-8`.
Without `pkg-config`, the build script scans common installation prefixes for
Linux distributions (Debian, Ubuntu, Fedora/RHEL/CentOS, openSUSE, Alpine),
FreeBSD, and macOS (including Homebrew) to locate the headers and
libraries.

If your system ships `libpcre2-8` under `/usr` but you also maintain a
manually built copy under `/usr/local`, export `PYPCRE_LIBRARY_PATH` (and, if
needed, a matching `PYPCRE_INCLUDE_DIR`) so the build links against the desired
location.
