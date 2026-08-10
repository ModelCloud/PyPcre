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
    <a href="https://github.com/ModelCloud/PyPcre/releases" style="text-decoration:none;"><img alt="GitHub release" src="https://img.shields.io/github/release/ModelCloud/Pcre.svg"></a>
    <a href="https://pypi.org/project/PyPcre/" style="text-decoration:none;"><img alt="PyPI - Version" src="https://img.shields.io/pypi/v/PyPcre"></a>
    <a href="https://pepy.tech/projects/PyPcre" style="text-decoration:none;"><img src="https://static.pepy.tech/badge/PyPcre" alt="PyPI Downloads"></a>
    <a href="https://github.com/ModelCloud/PyPcre/blob/main/LICENSE"><img src="https://img.shields.io/pypi/l/PyPcre"></a>
    <a href="https://huggingface.co/modelcloud/"><img src="https://img.shields.io/badge/🤗%20Hugging%20Face-ModelCloud-%23ff8811.svg"></a>
</p>



## Latest News 🚀
* 08/10/2026 **Adjacent literal-capture `split`**: the same bounded two-through-eight-capture descriptor now drives a call-local C splitter/assembler for exact patterns such as `(token)(-)(id)`. It allocates one final list, transfers temporary piece ownership without duplicate substring references, and inserts only immutable prevalidated captures. Pinned three-capture measurements improve 100/500 matches by **3.9x/4.5x** on Python 3.10 and **5.4x/6.5x** on free-threaded Python 3.14t/GIL=0; eight captures reach **5.4x/6.6x** and **8.2x/9.0x**, respectively. No new descriptor, cache, retained subject, or cross-call state is added; maxsplit translation, overflow arithmetic, text/bytes, subclasses, malformed private calls, 3,000 randomized cases, and shared-Pattern concurrency are covered. ⚡🛡️
* 08/10/2026 **Adjacent literal-capture `findall`**: exact default-option patterns made entirely from two through eight adjacent plain captures, such as `(token)(-)(id)`, now use one immutable literal count and the prevalidated capture tuple. Pinned two-capture measurements improve 100/500 matches by **7.5x/13.5x** on Python 3.10 and **10.8x/18.3x** on free-threaded Python 3.14t/GIL=0; eight captures reach **14.9x/24.5x** and **19.5x/29.4x**, respectively. The Pattern-local descriptor is capped at eight groups and 64 total literal units, lives only within the already bounded Pattern/cache lifetime, and never retains subjects or results. Metacharacters, intervening text, options, subclasses, ranges, and ninth-or-later groups keep the native PCRE2 path; 3,000 randomized parity cases and shared-Pattern stress cover Unicode, bytes, limits, and thread safety. ⚡🛡️
* 08/10/2026 **Literal-capture `split` ownership fast path**: exact default-option patterns containing one plain capture such as `(token)` now use CPython's immutable splitter and assemble the captured-delimiter result in C. Temporary piece references transfer directly into the final list, so the path performs no duplicate substring allocations and retains no subject, result, or call-local state. Pinned 100/500/2,000-capture measurements improve by **2.6x/2.8x/3.1x** on Python 3.10 and **3.5x/3.8x/3.8x** on free-threaded Python 3.14t/GIL=0; allocation of the API-required 2N+1 output objects is now the dominant floor. All negative and bounded limits, text/bytes, mixed Unicode kinds, subclasses, and shared-Pattern concurrency are covered against `stdlib.re`; the existing literal snapshot remains capped at 64 code units within the bounded Pattern lifetime. ⚡🛡️
* 08/10/2026 **Bounded literal-capture `findall`**: exact default-option patterns containing one plain capture such as `(token)` now use immutable `str`/`bytes.count` plus list construction. Pinned delimiter-heavy measurements improve 100/500 captures by **5.0x/7.0x** on Python 3.10 and **6.2x/8.4x** on free-threaded Python 3.14t/GIL=0. The per-Pattern literal snapshot is capped at 64 code units, lives only as long as that Pattern (including its already bounded thread-local cache entry), and never retains subjects or results; metacharacters, options, subclasses, and non-default ranges keep the native PCRE2 path. 20,000 randomized parity cases and shared-pattern stress cover text/bytes behavior. ⚡🛡️
* 08/10/2026 **UTF bytes compile safety**: bytes patterns can no longer bypass PCRE2's UTF validation by combining `UTF` with `NO_UTF_CHECK`. Malformed inputs previously violated a PCRE2 compiler precondition and could corrupt memory or crash concurrent free-threaded compilation; they now raise the precise PCRE error, while valid bytes preserve the requested flag and behavior. Subprocess fault tests and 8-thread invalid-pattern stress cover the boundary. The fix adds no cache, copy, or retained input and only validates calls that explicitly requested the unsafe bytes combination. 🛡️
* 08/10/2026 **Stateless 3.10 `template()` dispatch**: the deprecated compatibility helper now reuses its imported warning module and passes the precomputed default template flag directly, while dynamic flag objects and integer subclasses retain their original `__or__` dispatch. Pinned Python 3.10 calls improve from 4.81 μs at the merged base to 0.78 μs (**6.2x**); Python 3.14, where `re.TEMPLATE` no longer exists, remains effectively flat. Every call still warns, delegates to `compile`, and adds no cache or retained input. ⚡🛡️
* 08/10/2026 **Exact ovector `lastindex` shortcut**: a Match with zero or one participating capture now derives `lastindex`/`lastgroup` directly from its immutable ovector; only matches with multiple participating captures pay for the exact AUTO_CALLOUT ordering replay. Pinned measurements reduce the first-read `lastindex` portion by **11–15x** on Python 3.10 and **8–16x** on free-threaded Python 3.14t/GIL=0, while preserving nested/lookaround/duplicate-name semantics. The shortcut adds no object field, replay code, cache entry, or retained capture value; concurrent first publication still uses the Match critical section. ⚡🛡️
* 08/10/2026 **Stateless stdlib-flag dispatch**: exact `compile(pattern, re.RegexFlag)` calls now translate the finite stdlib bitset with plain integer probes and go directly to the existing bounded thread-local pattern cache. Pinned A/B measurements improve cached `re.I`, `re.I|re.M`, and `re.I|re.M|re.S|re.X` compilation by **6.2–6.7x** on Python 3.10 and **6.4–6.9x** on free-threaded Python 3.14t/GIL=0. All supported combinations are exhaustively checked for text and bytes, unsupported bits still raise, and the path adds no cache, retained flag object, or cross-thread state. ⚡🛡️
* 08/10/2026 **Call-local bounded substitution**: exact `Pattern.sub`/`subn` calls with counts from 2 through 8 now stay in PCRE2 for literal, numeric, explicit numeric, and named replacements. A stack-local substitute callout stops after the requested accepted replacement, is cleared before its match context can be reused, and uses a compact output buffer that grows geometrically only toward a strict linear ceiling. Pinned A/B measurements improve these bound forms by **4.5–8.6x** on Python 3.10 and **4.1–6.5x** on free-threaded Python 3.14t/GIL=0. The path retains no callback/template state and does not grow the replacement cache; count 9+, multiple/ambiguous references, subclasses, buffers, and callables remain on the compatibility loop. ⚡🛡️
* 08/10/2026 **Native count-one substitution**: exact `Pattern.sub`/`subn` calls with `count=1` now stay in PCRE2 for literal, numeric, explicit numeric, and named replacements instead of rebuilding the bounded result through a Python match loop. Pinned A/B measurements improve the four bound forms by **4.2–4.7x** on Python 3.10 and **4.1–4.3x** on free-threaded Python 3.14t/GIL=0; module-level forms improve by **2.4–3.2x**. Translation is call-local and never grows the replacement-template cache; count 9+, ambiguous templates, subclasses, mutable buffers, and callables retain the compatibility path. ⚡🛡️
* 08/10/2026 **Stateless `escape` fast path**: exact immutable text and bytes now use a native `re.escape`-compatible scanner with no cache, retained parsing state, or cross-thread ownership. Pinned A/B measurements against the previous Python wrapper improve short no-op text by **5.0x/3.6x** on Python 3.10/3.14t and no-op bytes by **6.9x/6.5x**; short escaped punctuation improves by **3.6x/3.1x**. Mutable buffers and subclasses continue through stdlib coercion/dynamic dispatch, and exhaustive byte plus randomized Unicode parity checks cover the native path. ⚡🛡️
* 08/10/2026 **Call-local single-reference substitution**: exact default-count `Pattern.sub`/`subn` replacements containing one valid numeric, explicit numeric, or named capture now translate and execute entirely in C without entering or growing the thread-local replacement-template cache. Pinned A/B measurements improve short numeric and explicit forms by roughly **1.9–2.1x** on Python 3.10 and free-threaded Python 3.14t/GIL=0; named replacement improves by **10.9x/1.9x**. Duplicate PCRE names select the participating capture, while `$`, multiple/ambiguous references, subclasses, and counts above eight retain the compatibility parser. ⚡🛡️
* 08/10/2026 **Call-local `Match.expand` fast paths**: exact text and bytes templates containing up to eight unambiguous capture/backslash tokens now render directly from the immutable C Match snapshot, without importing the compatibility parser or retaining a parsed-template cache entry. Pinned A/B measurements against merged `main` improve matched `[\\1]` expansion by **20.8x** on Python 3.10 and **16.7x** on free-threaded Python 3.14t/GIL=0; unmatched captures reach **25.5x/20.7x**, and bytes reach **37.4x/31.7x**. Explicit numeric `[\\g<1>]` improves by **14.3x/13.8x**, checked multi-digit references such as `\\g<12>` reach **63.3x/13.1x**, named `[\\g<word>]` improves by **28.7x/10.6x**, and a two-name template reaches **45.3x/9.6x**. Three references reach **9.2x/8.2x**, while eight reach **6.3x/6.3x**. A literal backslash plus named capture reaches **13.9x/11.7x**, and a named capture with a backslash suffix reaches **42.9x/9.2x**. Duplicate-name alternatives select the participating capture. Nine-or-more tokens, non-backslash escapes, ambiguous two-digit, subclass, invalid, and non-ASCII-name templates continue through the fully compatible parser. ⚡🛡️
* 08/10/2026 **Literal split/substitution/findall fast paths**: exact plain-literal `Pattern.split` calls now use the immutable built-in splitter after construction-time validation, measuring **2.1x** faster than the prior C dispatch on Python 3.10 and **1.7x** faster on free-threaded Python 3.14t/GIL=0; delimiter-heavy multi-character literals reach roughly **4.8x**. Literal `Pattern.subn` and module-level `sub`/`subn` now use native replace/count primitives, reaching about **15x** on short repeated tokens and **3x** on delimiter-heavy text. Literal `findall` uses non-overlapping native count/list construction, reaching about **9x** on short repeated tokens and **8x** on delimiter-heavy text. Regex metacharacters, explicit flags, subclasses, and buffer subjects remain on the compatibility-safe PCRE2 path. ⚡
* 08/09/2026 **API hot-path update**: large `parallel_map(findall)` workloads now reach **11.5x** speedup on Python 3.10 and **11.25x** on free-threaded Python 3.14t/GIL=0 with 12 performance-tier workers. Ordered `parallel_map(search)` reaches **8.57x** and **7.85x**, respectively; one-item and up to eight tiny explicit `parallel_map` subjects now avoid executor setup (the one-item case measures **13.3x** faster on Python 3.10 and **27.7x** on 3.14t), default bound `Pattern.split` is another **1.6x/1.5x** faster on 3.10/3.14t, and default bound literal `Pattern.subn` is about **1.5x** faster on Python 3.10. Canonical module helpers retain their optimized dispatch while their wrapper/template caches are thread-scoped, size-bounded, and invalidated across live workers. Repeated backreference `Match.expand()` avoids reparsing within the active cache context, while captured values returned by `Match.groups()` remain call-local so a long-lived Match does not retain an additional copy of large captures. 🧵⚡
* 08/08/2026 **0.6.0**: `findall`, `finditer`, `sub`/`subn`, `split`, and `match`/`search`/`fullmatch` are now up to **46x faster** than `stdlib.re` and **48x faster** than `regex` on `finditer`/`findall` workloads, **13x** on `split`, and **2–9x** on `sub`/`subn` backref workloads, with full `re` semantics. Free-threaded `findall` reaches **13.8x** vs `re` on 8 threads. 🚀⚡
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
- 🧵 **Thread-safe into `nogil`**: PyPcre is built for `PYTHON_GIL=0`, with CI coverage, lock-aware caches, reusable match/JIT resources, and `parallel_map()` for multi-subject fan-out.
- ⚡ **Fast on real workloads**: `PCRE2` JIT plus cached compiled patterns lets PyPcre match or beat `re` and `regex` on many common scans, especially multiline searches, lookaround-heavy patterns, and free-threaded execution.
- 🛡️ **Safer operational story**: PyPcre prefers the system `libpcre2-8` shared library so normal OS package updates can bring security and bug-fix benefits without a bundled fork.
- ✅ **Validated thoroughly**: the project runs API tests, fuzz tests, memory-safety checks, local `valgrind` leak checks, and `massif` heap profiles. Recent local profiling found `0` definite leaks and `0` possible leaks in both the public API and raw binding paths.

### Quick Comparison 🥊

| Area | PyPcre | `stdlib.re` | `regex` |
| --- | --- | --- | --- |
| Engine | Full `PCRE2` ✅ | CPython stdlib engine | Separate engine, not `PCRE2` |
| `PCRE2` syntax and flags | Full access ✅ | No | No |
| Syntax power | Very rich ✅ | More limited | Rich, but different from `PCRE2` |
| JIT execution | `PCRE2` JIT ✅ | No | No |
| `re`-compatible API surface | Stable and familiar ✅ | Native | Similar, but not the main goal |
| Free-threaded support | Built and tested for `PYTHON_GIL=0` ✅ | No explicit PyPcre-style layer | Not a project focus here |
| Built-in threaded subject fan-out | `parallel_map()` ✅ | No | No |
| System library updates | Uses system `libpcre2-8` by default ✅ | N/A | N/A |

### Benchmark Highlights 🏁

#### API hot paths and 12-core fan-out

Pinned A/B measurements on an Apple M4 Max use the same `taskpolicy -t 1 -l 1`
scheduler policy for both interpreters. The host reports 12 performance logical
CPUs and 4 efficiency logical CPUs; macOS does not provide an unprivileged hard
per-process CPU mask, so the benchmark records the topology rather than claiming
hard CPU affinity.

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

The parallel figures are serial-to-parallel speedups and preserve input order and
exception behavior. Large `findall` scans release the GIL only around the PCRE2
call; match data, context, and subject ownership remain worker-local. Reproduce
the fan-out benchmark with:

```bash
taskpolicy -t 1 -l 1 env PYTHONPATH=. \
  PYPCRE_PARALLEL_WORKERS=12 PYPCRE_PARALLEL_RUNS=3 \
  python3 benchmarks/parallel_map_hotpath.py
```

The same script runs under Python 3.14t. The API microbenchmarks are available in
[`benchmarks/api_hotpaths.py`](benchmarks/api_hotpaths.py).

Measured on a `Python 3.14.6` free-threaded build on x86_64 Linux with compiled-pattern reuse and JIT enabled. Times are the best of several runs; lower is better. Only workloads where PyPcre is decisively faster than both `stdlib.re` and `regex` are shown.

A reproducible version of this benchmark lives in [`benchmarks/competitor_bench.py`](benchmarks/competitor_bench.py).

#### `findall` — large multiline and lookaround workloads

| Workload | PyPcre (ms) | `re` (ms) | `regex` (ms) | PyPcre edge |
| --- | ---: | ---: | ---: | --- |
| Extract `WARN` / `ERROR` lines (multiline) | `0.664` | `27.159` | `30.772` | **40.9x** vs `re`, **46.3x** vs `regex` |
| Per-line full-name extraction (multiline) | `0.914` | `25.685` | `14.482` | **28.1x** vs `re`, **15.8x** vs `regex` |
| Lookbehind + negative-lookahead tokens | `1.874` | `12.353` | `10.386` | **6.6x** vs `re`, **5.5x** vs `regex` |

Patterns used:

```python
# WARN/ERROR lines and full-name extraction
^(?:WARN|ERROR).*?$        # with re.MULTILINE / pcre.Flag.MULTILINE
^[A-Z][a-z]+ [A-Z][a-z]+   # with re.MULTILINE / pcre.Flag.MULTILINE

# lookbehind + negative lookahead
(?:(?<=foo)bar|baz)(?!qux)
```

#### `finditer` — same workloads

Measured on a Python 3.10 x86_64 Linux build with compiled-pattern reuse and JIT enabled. A reproducible version lives in [`benchmarks/finditer_bench.py`](benchmarks/finditer_bench.py).

| Workload | PyPcre (ms) | `re` (ms) | `regex` (ms) | PyPcre edge |
| --- | ---: | ---: | ---: | --- |
| Extract `WARN` / `ERROR` lines | `0.663` | `30.747` | `31.862` | **46.4x** vs `re`, **48.0x** vs `regex` |
| Per-line full-name extraction | `0.919` | `29.127` | `14.103` | **31.7x** vs `re`, **15.3x** vs `regex` |
| Lookbehind + negative lookahead | `3.755` | `15.828` | `11.896` | **4.2x** vs `re`, **3.2x** vs `regex` |

#### `sub` / `subn` — high-volume replacement workloads

Measured on a Python 3.10 x86_64 Linux build with compiled-pattern reuse and JIT enabled. Times are the best of several runs; lower is better. The benchmark replaces 100,000 space-separated tokens.

A reproducible version lives in [`benchmarks/sub_bench.py`](benchmarks/sub_bench.py).

| Workload | PyPcre (ms) | `re` (ms) | `regex` (ms) | PyPcre edge |
| --- | ---: | ---: | ---: | --- |
| Literal replacement (`\w+` → `[X]`) | `5.933` | `13.367` | `19.107` | **2.3x** vs `re`, **3.2x** vs `regex` |
| Single numeric backref (`(w)\d+` → `[\1]`) | `7.221` | `64.715` | `25.482` | **9.0x** vs `re`, **3.5x** vs `regex` |
| Two numeric backrefs (`(w)(\d+)` → `\2-\1`) | `17.600` | `76.222` | `32.048` | **4.3x** vs `re`, **1.8x** vs `regex` |
| Named backref (`(?P<g>\w+)` → `<\g<g>>`) | `14.684` | `71.053` | `30.222` | **4.8x** vs `re`, **2.1x** vs `regex` |

#### `split` — high-volume delimiter workloads

Measured on a Python 3.10 x86_64 Linux build with compiled-pattern reuse and JIT enabled. Times are the best of several runs; lower is better. The benchmark splits 100,000 space-separated tokens.

A reproducible version lives in [`benchmarks/split_bench.py`](benchmarks/split_bench.py).

| Workload | PyPcre (ms) | `re` (ms) | `regex` (ms) | PyPcre edge |
| --- | ---: | ---: | ---: | --- |
| Delimiter no group (`\s+`) | `7.315` | `17.394` | `19.690` | **2.4x** vs `re`, **2.7x** vs `regex` |
| Delimiter with group (`(\s+)`) | `12.360` | `21.099` | `25.145` | **1.7x** vs `re`, **2.0x** vs `regex` |
| Single char (` `) | `4.583` | `3.990` | `14.577` | parity vs `re`, **3.2x** vs `regex` |
| Single char with group (`( )`) | `8.913` | `11.091` | `18.595` | **1.2x** vs `re`, **2.1x** vs `regex` |
| Empty pattern (`''`) | `5.228` | `45.552` | `69.913` | **8.7x** vs `re`, **13.4x** vs `regex` |

### Free-Threaded Benchmark Highlights 🧵

Measured on the same `Python 3.14.6` free-threaded build with `8` threads fanning out over split copies of each workload. Times are the best of several runs; lower is better.

A reproducible version lives in [`benchmarks/free_threaded_bench.py`](benchmarks/free_threaded_bench.py).

| Workload | PyPcre (ms) | `re` (ms) | `regex` (ms) | PyPcre edge |
| --- | ---: | ---: | ---: | --- |
| Extract `WARN` / `ERROR` lines (`findall`) | `0.672` | `9.063` | `9.297` | **13.5x** vs `re`, **13.8x** vs `regex` |
| Per-line full-name extraction (`findall`) | `0.913` | `8.611` | `4.575` | **9.4x** vs `re`, **5.0x** vs `regex` |

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
  `findall`, `split`, `sub`, `subn`, `compile`, `escape`, `purge`, and
  `parallel_map`.
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

Compiled patterns:

```python
from pcre import compile, Flag

pattern = compile(r"(?P<name>[A-Za-z]+)", flags=Flag.CASELESS)
match = pattern.search("User: alice")
print(match.group("name"))  # alice
```

Substitution:

```python
from pcre import sub

result = sub(r"\d+", "#", "room 101")
print(result)  # room #
```

Bytes:

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

- `pcre.configure(jit=False)` disables JIT globally. `Flag.JIT` and
  `Flag.NO_JIT` let you override that per pattern.
- `pcre.set_cache_limit()`, `pcre.get_cache_limit()`, and `pcre.clear_cache()`
  control every high-level compile/template helper cache in the active context.
  A zero limit disables them, and `None` uses a 256-entry hard safety ceiling
  rather than permitting unbounded growth. High-level cache entries never cross
  thread scope in the default thread-local strategy; a clear invalidates live
  workers' high-level helper caches on their next cache-backed call. Backend
  scratch buffers remain thread-scoped and are released when that thread exits.
  Oversized patterns and templates are not retained.
- `pcre.configure_threads()`, `pcre.configure_thread_pool()`,
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
