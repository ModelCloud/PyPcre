<!--
# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium
-->

# PyPcre (Python PCRE2 Binding)

Modern `nogil` Python bindings for the PCRE2 library with `stdlib.re` API compatibility.

<p align="center">
    <a href="https://github.com/ModelCloud/PyPcre/releases" style="text-decoration:none;"><img alt="GitHub release" src="https://img.shields.io/github/release/ModelCloud/Pcre.svg"></a>
    <a href="https://pypi.org/project/PyPcre/" style="text-decoration:none;"><img alt="PyPI - Version" src="https://img.shields.io/pypi/v/PyPcre"></a>
    <a href="https://pepy.tech/projects/PyPcre" style="text-decoration:none;"><img src="https://static.pepy.tech/badge/PyPcre" alt="PyPI Downloads"></a>
    <a href="https://github.com/ModelCloud/PyPcre/blob/main/LICENSE"><img src="https://img.shields.io/pypi/l/PyPcre"></a>
    <a href="https://huggingface.co/modelcloud/"><img src="https://img.shields.io/badge/🤗%20Hugging%20Face-ModelCloud-%23ff8811.svg"></a>
</p>



## Latest News
* 03/22/2026 [0.2.15](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.15): Python 3.15 `re` compatibility (`prefixmatch`, `NOFLAG`)
* 03/21/2026 [0.2.14](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.14): Python 3.14 compatibility
* 03/02/2026 [0.2.11](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.11): Auto-detect `Visual Studio` in Windows environments during install and compile.
* 02/24/2026 [0.2.10](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.10): Allow a `Visual Studio` (VS) compiler version check override via an environment variable.
* 12/15/2025 [0.2.8](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.8): Fixed multi-arch Linux OS compatibility when both x86_64 and i386 `pcre2` libraries are installed.
* 10/20/2025 [0.2.4](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.4): Removed the dependency on a system `python3-dev` package. `Python.h` will be downloaded optimistically from python.org when needed.
* 10/12/2025 [0.2.3](https://github.com/ModelCloud/PyPcre/releases/tag/v0.2.3): 🤗 Full `GIL=0` compliance for Python >= 3.13T. Reduced cache thread contention. Improved performance across all APIs. Expanded CI test coverage. FreeBSD, Solaris, and Windows compatibility validated.
* 10/09/2025 [0.1.0](https://github.com/ModelCloud/PyPcre/releases/tag/v0.1.0): 🎉 First release. Thread-safe, with auto JIT, auto pattern caching, and optimistic linking to the system library for fast installs.

## Why PyPcre

PyPcre is a modern PCRE2 binding designed to be both fast and thread-safe in a `GIL=0` world. In the era of the global interpreter lock, Python had real threads but often only limited concurrency, aside from a handful of low-level APIs and packages. As Python moves toward a fuller `GIL=0` design, true multi-threaded concurrency becomes practical and brings Python closer to parity with other modern languages.

Many Python regular expression packages either segfault under `GIL=0` or suffer suboptimal performance because they were not designed with threaded execution in mind.

PyPcre is fully CI-tested. Every API and PCRE2 flag is exercised in a continuous development environment backed by the ModelCloud.AI team. Fuzz (clobber) tests are also run to catch memory safety, accuracy, and memory leak regressions.

For safety, PyPcre preferentially links against the OS-provided `libpcre2` package so it can benefit from upstream security patches. You can force a full source build with the `PYPCRE_BUILD_FROM_SOURCE=1` environment variable.

## Installation

```bash
pip install PyPcre
```

The package prefers linking against the system `libpcre2-8` shared library for fast installs and to inherit security updates from the OS. See [Building](#building) for manual build details.

## Platform Support (Validated)

`Linux`, `macOS`, `Windows`, `WSL`, `FreeBSD`


## Usage

If you already rely on the standard library `re`, migrating is as
simple as changing your import:

```python
import pcre as re
```

The high-level API keeps the standard library shape, so most existing `re`
code can move over with little or no rewriting.

### Quick start

```python
from pcre import compile, findall, match, search, Flag

if match(r"(?P<word>\\w+)", "hello world"):
    print("found word")

pattern = compile(rb"\d+", flags=Flag.MULTILINE)
numbers = pattern.findall(b"line 1\nline 22")
```

### User-facing API

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

### Common examples

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

### Stdlib `re` compatibility

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

Set the default behavior globally with `pcre.configure(compat_regex=True)`
so that subsequent calls to `compile()` and the module-level helpers apply
the conversion without repeating the flag.

### Common issues

- Unsupported stdlib flags such as `re.DEBUG`, `re.LOCALE`, and `re.ASCII`
  raise `ValueError`. If you want ASCII-style behavior, use `pcre.ASCII` or
  `Flag.NO_UTF | Flag.NO_UCP`.
- Replacement types must match the subject type: text patterns use `str`
  replacements, while byte patterns use bytes-like replacements.
- If you are porting patterns from the third-party `regex` package, check
  `\u` and `\U` escapes first. That is the most common compatibility gap.
- Most users do not need to tune caching, JIT, or threading. The defaults are
  intended to work well out of the box.

### Optional runtime controls

- `pcre.configure(jit=False)` disables JIT globally. `Flag.JIT` and
  `Flag.NO_JIT` let you override that per pattern.
- `pcre.set_cache_limit()`, `pcre.get_cache_limit()`, and `pcre.clear_cache()`
  control the high-level compile cache.
- `pcre.configure_threads()`, `pcre.configure_thread_pool()`,
  `shutdown_thread_pool()`, `Flag.THREADS`, and `Flag.NO_THREADS` are available
  if you want to opt into or restrict threaded execution.

## Building

The extension links against an existing PCRE2 installation (the `libpcre2-8`
variant). Install the development headers for your platform before building,
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
