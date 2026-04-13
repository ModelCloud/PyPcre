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

Measured on a `Python 3.14.3` free-threaded build on x86_64 Linux with compiled-pattern reuse. Times are best-of-5; lower is better.

| Workload | Operation | PyPcre | `re` | `regex` | PyPcre edge |
| --- | --- | ---: | ---: | ---: | --- |
| First `ERROR` line in a multiline log buffer | `search` | `3.68 ms` | `51.72 ms` | `5.67 ms` | `14.0x` vs `re`, `1.54x` vs `regex` |
| Extract only `WARN` / `ERROR` lines | `findall` | `6.41 ms` | `91.84 ms` | `91.14 ms` | `14.3x` vs `re`, `14.2x` vs `regex` |
| Per-line full-name extraction | `findall` | `22.28 ms` | `172.38 ms` | `218.29 ms` | `7.74x` vs `re`, `9.80x` vs `regex` |
| Lookbehind + negative-lookahead extraction | `findall` | `50.23 ms` | `53.35 ms` | `57.03 ms` | `1.06x` vs `re`, `1.14x` vs `regex` |
| UUID extraction | `findall` | `77.49 ms` | `83.19 ms` | `134.87 ms` | `1.07x` vs `re`, `1.74x` vs `regex` |
| Boundary-aware token scan | `findall` | `127.76 ms` | `128.03 ms` | `146.37 ms` | effectively tied with `re`, `1.15x` vs `regex` |

### Free-Threaded Benchmark Highlights 🧵

Measured in the same environment with `8` threads sharing one compiled pattern. Times are best-of-3; lower is better.

| Workload | Threads | PyPcre | `re` | `regex` | PyPcre edge |
| --- | ---: | ---: | ---: | ---: | --- |
| First `ERROR` line in a multiline log buffer | `8` | `25.34 ms` | `38.83 ms` | `40.34 ms` | `1.53x` vs `re`, `1.59x` vs `regex` |
| Extract only `WARN` / `ERROR` lines | `8` | `28.58 ms` | `65.54 ms` | `73.55 ms` | `2.29x` vs `re`, `2.57x` vs `regex` |
| Per-line full-name extraction | `8` | `31.68 ms` | `123.44 ms` | `164.80 ms` | `3.90x` vs `re`, `5.20x` vs `regex` |

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
  control the high-level compile cache.
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
