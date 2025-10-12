<!--
# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium
-->

# PyPcre (Python Pcre2 Binding)

Modern `nogil` Python bindings for the Pcre2 library with `stdlib.re` api compatibility.

<p align="center">
    <a href="https://github.com/ModelCloud/PyPcre/releases" style="text-decoration:none;"><img alt="GitHub release" src="https://img.shields.io/github/release/ModelCloud/Pcre.svg"></a>
    <a href="https://pypi.org/project/PyPcre/" style="text-decoration:none;"><img alt="PyPI - Version" src="https://img.shields.io/pypi/v/PyPcre"></a>
    <!-- <a href="https://pepy.tech/projects/PyPcre" style="text-decoration:none;"><img src="https://static.pepy.tech/badge/PyPcre" alt="PyPI Downloads"></a> -->
    <a href="https://github.com/ModelCloud/PyPcre/blob/main/LICENSE"><img src="https://img.shields.io/pypi/l/PyPcre"></a>
    <a href="https://huggingface.co/modelcloud/"><img src="https://img.shields.io/badge/🤗%20Hugging%20Face-ModelCloud-%23ff8811.svg"></a>
</p>

## Latest News
* 10/12/2025 [0.2.3](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.3): 🤗 Full `GIL=0` compliance for Python >= 3.13T. Reduced cache thread contention. Improved performance for all api. Expanded ci testing coverage. FreeBSD, Solaris, and Windows compatibility validated.
* 10/09/2025 [0.1.0](https://github.com/ModelCloud/PyPcre/releases/tag/v0.1.0): 🎉 First release. Thread safe, auto JIT, auto pattern caching and optimistic linking to system library for fast install.

## Why PyPcre:

PyPcre is a modern Pcre2 binding designed to be both super fast and thread-safe in the `GIL=0` world. In the old days of global interpreter locks, Python had real threads but mostly fake concurrency (with the exception of some low-level apis and packages). In 2025, Python is moving toward full `GIl=0` design which will unlock true multi-threaded concurrency and finally bring Python in parity with other modern languages. 

Many Python regular expression packages will either out-right segfault due to safety under `GIL=0` or suffer sub-optimal performance due to non-threaded design mindset. 

PyPcre is fully ci tested where every single api and Pcre2 flag is tested in a continuous development environment backed by the ModelCloud.AI team. Fuzz (clobber) tests are also performed to catch any memory safety, accuracy, or memory leak regressions. 

Safety first: PyPcre will optimistically link to the os provided `libpcre2` package for maximum safetey since PyPcre will automatically enjoy upstream security patches. You can force full source compile via `PYPCRE_BUILD_FROM_SOURCE=1` env toggle.

## Installation

```bash
pip install PyPcre
```

The package prioritizes linking against the `libpcre2-8` shared library in system for fast install and max security protection which gets latest patches from OS. See [Building](#building) for manual build details.

## Platform Support (Validated):

`Linux`, `MacOS`, `Windows`, `WSL`, `FreeBSD`, `Solaris`


## Usage


If you already rely on the standard library `re`, migrating is as
simple as changing your import:

```python
import pcre as re
```

The module-level entry points (`match`, `search`, `fullmatch`, `findall`,
`finditer`, `split`, `sub`, `subn`, `compile`, `escape`, `purge`) expose the
same call signatures as their `re` counterparts, making existing code work
unchanged. Every standard flag with a PCRE2 equivalent—`IGNORECASE`,
`MULTILINE`, `DOTALL`, `VERBOSE`, `ASCII`, and friends—is supported via the
re-exported constants and the `pcre.Flag` enum. 

### Sample Usage

```python
from pcre import match, search, findall, compile, Flag

if match(r"(?P<word>\\w+)", "hello world"):
    print("found word")

pattern = compile(rb"\d+", flags=Flag.MULTILINE)
numbers = pattern.findall(b"line 1\nline 22")
```

`pcre` mirrors the core helpers from Python’s standard library `re` module 
`match`, `search`, `fullmatch`, `finditer`, `findall`, and `compile` while
exposing PCRE2’s extended flag set through the Pythonic `Flag` enum
(`Flag.CASELESS`, `Flag.MULTILINE`, `Flag.UTF`, ...).

### Stdlib `re` compatibility

- Module-level helpers and the `Pattern` class follow the same call shapes as
  the standard library `re` module, including `pos`, `endpos`, and `flags`
  behaviour.
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

### `regex` package compatibility

The [`regex`](https://pypi.org/project/regex/) package interprets
`\uXXXX` and `\UXXXXXXXX` escapes as UTF-8 code points, while PCRE2 expects
hexadecimal escapes to use the `\x{...}` form. Enable `Flag.COMPAT_UNICODE_ESCAPE` to
translate those escapes automatically when compiling patterns:

```python
from pcre import compile, Flag

pattern = compile(r"\\U0001F600", flags=Flag.COMPAT_UNICODE_ESCAPE)
assert pattern.pattern == r"\\x{0001F600}"
```

Set the default behaviour globally with `pcre.configure(compat_regex=True)`
so that subsequent calls to `compile()` and the module-level helpers apply
the conversion without repeating the flag.

### Automatic pattern caching

`pcre.compile()` caches the final `Pattern` wrapper for up to 128
unique `(pattern, flags)` pairs when the pattern object is hashable. By default
the cache is **thread-local**, keeping per-thread LRU stores so workers do not
contend with one another. Adjust the capacity with `pcre.set_cache_limit(n)`—pass
`0` to disable caching completely or `None` for an unlimited cache—and check the
current limit with `pcre.get_cache_limit()`. The cache can be emptied at any time
with `pcre.clear_cache()`.

Applications that prefer the historic global cache can opt back in before any
compilation takes place by setting `PYPCRE_CACHE_PATTERN_GLOBAL=1` in the
environment **before importing** `pcre`. Runtime switching is no longer
supported; altering the value after patterns have been compiled raises
`RuntimeError`.

### Text versus bytes defaults

String patterns follow the same defaults as Python’s `re` module,
automatically enabling the `Flag.UTF` and `Flag.UCP` options so Unicode
pattern and character semantics “just work.” Byte patterns remain raw by
default—neither option is activated—so you retain full control over
binary-oriented matching. Explicitly set `Flag.NO_UTF`/`Flag.NO_UCP` if you
need to opt out for strings, or add the UTF/UCP flags yourself when compiling
bytes.

### Working with compiled patterns

- `compile()` accepts either a pattern literal or an existing `Pattern`
  instance, making it easy to mix compiled objects with the convenience
  helpers.
- `Pattern.match/search/fullmatch/finditer/findall` accept optional
  `pos`, `endpos`, and `options` arguments, mirroring the standard library
  `re` module while letting you thread PCRE2 execution flags through
  individual calls.

### Threaded execution

- `pcre.parallel_map()` fans out work across a shared thread pool for
  `match`, `search`, `fullmatch`, and `findall`. The helper preserves the
  order of the provided subjects and returns the same result objects you’d
  normally receive from the `Pattern` methods.
- The threaded backend activates only on machines with at least eight CPU
  cores; otherwise execution falls back to the sequential path regardless of
  flags or configuration.
- Threading is **opt-in by default** when Python runs without the GIL
  (e.g. Python with `-X gil=0` or `PYTHON_GIL=0`). When the GIL is active the default falls
  back to sequential execution to avoid needless overhead.
- With auto threading enabled (`configure_threads(enabled=True)`), the pool
  is only engaged when at least one subject is larger than the configured
  threshold (60 kB by default). Smaller jobs run sequentially to avoid the
  cost of thread hand-offs; adjust the boundary via
  `configure_threads(threshold=...)`.
- Use `Flag.THREADS` to force threaded execution for a specific pattern or
  `Flag.NO_THREADS` to lock it to sequential mode regardless of global
  settings.
- `pcre.configure_thread_pool(max_workers=...)` controls the size of the
  shared executor (capped to half the available CPUs); call it with
  `preload=True` to spin the pool up eagerly, and `shutdown_thread_pool()`
  to tear it down manually if needed.

### Performance considerations

- **Precompile for hot loops.** The module-level helpers mirror the `re`
  API and route through the shared compilation cache, but the extra call
  plumbing still adds overhead. With a simple pattern like `"fo"`, using
  the low-level `pcre_ext_c.Pattern` directly costs ~0.60 µs per call,
  whereas the high-level `pcre.match()` helper lands at ~4.4 µs per call
  under the same workload. For sustained loops, create a `Pattern` object
  once and reuse it.
- **Benchmark toggles.** The extension defaults to the fastest safe
  configuration, but you can flip individual knobs back to the legacy
  behaviour by setting environment variables *before* importing `pcre`:

  | Env var                        | Effect (per-call, `pattern.match("fo")`) |
  |--------------------------------|------------------------------------------|
  | _(baseline)_                   | 0.60 µs                                  |
  | `PYPCRE_DISABLE_CONTEXT_CACHE=1` | 0.60 µs |
  | `PYPCRE_FORCE_JIT_LOCK=1`       | 0.60 µs |
  | `pcre.match()` helper          | 4.43 µs                                  |

  The toggles reintroduce the legacy GIL hand-off, per-call match-context
  allocation, and explicit locks so you can quantify the impact of each
  optimisation on your workload. Measurements were taken on CPython 3.14 (rc3)
  with 200 000 evaluations of `pcre_ext_c.compile("fo").match("foobar")`; absolute
  values will vary by platform, but the relative differences are
  representative. Leave the variables unset in production to keep the new fast
  paths active.

### JIT Pattern Compilation and Execution

Pcre2’s JIT compiler is enabled by default for every compiled pattern. The
wrapper exposes two complementary ways to adjust that behaviour:

- Toggle the global default at runtime with `pcre.configure(jit=False)` to
  turn JIT off (call `pcre.configure(jit=True)` to turn it back on).
- Override the default per pattern using the Python-only flags `Flag.JIT`
  and `Flag.NO_JIT`:

  ```python
  from pcre import compile, configure, Flag

  configure(jit=False)              # disable JIT globally
  baseline = compile(r"expr")      # JIT disabled

  fast = compile(r"expr", flags=Flag.JIT)      # force-enable for this pattern
  slow = compile(r"expr", flags=Flag.NO_JIT)   # force-disable for this pattern
  ```

## Pattern cache
- `pcre.compile()` caches hashable `(pattern, flags)` pairs, keeping up to 128 entries per thread by default.
- Set `PYPCRE_CACHE_PATTERN_GLOBAL=1` before importing `pcre` if you need a shared, process-wide cache instead of isolated thread stores.
- Use `pcre.clear_cache()` when you need to free the active cache proactively.
- Non-hashable pattern objects skip the cache and are compiled each time.

## Default flags for text patterns
- String patterns enable `Flag.UTF` and `Flag.UCP` automatically so behaviour matches `re`.
- Byte patterns keep both flags disabled; opt in manually if Unicode semantics are desired.
- Explicitly supply `Flag.NO_UTF`/`Flag.NO_UCP` to override the defaults for strings.

## Additional usage notes
- All top-level helpers (`match`, `search`, `fullmatch`, `finditer`, `findall`) defer to the cached compiler.
- Compiled `Pattern` objects expose `.pattern`, `.flags`, `.jit`, and `.groupindex` for introspection.
- Execution helpers accept `pos`, `endpos`, and `options`, allowing you to thread PCRE2 execution flags per call.

## Memory allocation
- By default PyPcre uses CPython's `PyMem` allocator.
- Override the allocator explicitly by setting `PYPCRE_ALLOCATOR` to one of
  `pymem`, `malloc`, `jemalloc`, or `tcmalloc` before importing the module. The
  optional allocators are still loaded with `dlopen`, so no additional link
  flags are required when they are absent.
- Call `pcre_ext_c.get_allocator()` to inspect which backend is active at
  runtime.

## Building

The extension links against an existing PCRE2 installation (the `libpcre2-8`
variant). Install the development headers for your platform before building,
for example `apt install libpcre2-dev` on Debian/Ubuntu, `dnf install pcre2-devel`
on Fedora/RHEL derivatives, or `brew install pcre2` on macOS.

If the headers or library live in a non-standard location you can export one
or more of the following environment variables prior to invoking the build
(`pip install .`, `python -m build`, etc.):

- `PYPCRE_ROOT`
- `PYPCRE_INCLUDE_DIR`
- `PYPCRE_LIBRARY_DIR`
- `PYPCRE_LIBRARY_PATH` *(pathsep-separated directories or explicit library files to
  prioritise when resolving `libpcre2-8`)*
- `PYPCRE_LIBRARIES`
- `PYPCRE_CFLAGS`
- `PYPCRE_LDFLAGS`

When `pkg-config` is available the build will automatically pick up the
required include and link flags via `pkg-config --cflags/--libs libpcre2-8`.
Without `pkg-config`, the build script scans common installation prefixes for
Linux distributions (Debian, Ubuntu, Fedora/RHEL/CentOS, openSUSE, Alpine),
FreeBSD, macOS (including Homebrew), and Solaris to locate the headers and
libraries.

If your system ships `libpcre2-8` under `/usr` but you also maintain a
manually built copy under `/usr/local`, export `PYPCRE_LIBRARY_PATH` (and, if
needed, a matching `PYPCRE_INCLUDE_DIR`) so the build links against the desired
location.
