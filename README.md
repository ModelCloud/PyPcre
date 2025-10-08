# Pcre (Python Pcre2 Binding)

Python bindings for the system PCRE2 library with a familiar `re`-style API.

## Installation

```bash
pip install pcre
```

The package links against the `libpcre2-8` variant already available on your
system. See [Building](#building) for manual build details.

## Usage

### Drop-in helpers

```python
from pcre import match, search, findall, compile, Flag

if match(r"(?P<word>\\w+)", "hello world"):
    print("found word")

pattern = compile(rb"\d+", flags=Flag.MULTILINE)
numbers = pattern.findall(b"line 1\nline 22")
```

`pcre` mirrors the core helpers from Python’s standard library `re` module—
`match`, `search`, `fullmatch`, `finditer`, `findall`, and `compile`—while
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

### Automatic pattern caching

`pcre.compile()` caches the final `Pattern` wrapper for up to 2048
unique `(pattern, flags)` pairs when the pattern object is hashable. This
keeps repeated calls to top-level helpers efficient without any extra work
from the caller. The cache can be emptied with `pcre.clear_cache()` if your
application needs to release memory proactively.

Non-hashable patterns (for example, custom objects) bypass the cache and are
still compiled immediately.

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

### JIT control

Pcre’s JIT compiler is enabled by default for every compiled pattern. The
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

## Building

The extension links against an existing PCRE2 installation (the `libpcre2-8`
variant). Install the development headers for your platform before building,
for example `apt install libpcre2-dev` on Debian/Ubuntu, `dnf install pcre2-devel`
on Fedora/RHEL derivatives, or `brew install pcre2` on macOS.

If the headers or library live in a non-standard location you can export one
or more of the following environment variables prior to invoking the build
(`pip install .`, `python -m build`, etc.):

- `PCRE2_ROOT`
- `PCRE2_INCLUDE_DIR`
- `PCRE2_LIBRARY_DIR`
- `PCRE2_LIBRARIES`
- `PCRE2_CFLAGS`
- `PCRE2_LDFLAGS`

When `pkg-config` is available the build will automatically pick up the
required include and link flags via `pkg-config --cflags/--libs libpcre2-8`.
Without `pkg-config`, the build script scans common installation prefixes for
Linux distributions (Debian, Ubuntu, Fedora/RHEL/CentOS, openSUSE, Alpine),
FreeBSD, macOS (including Homebrew), and Solaris to locate the headers and
libraries.

# Notes

## Pattern cache
- `pcre.compile()` caches hashable `(pattern, flags)` pairs, keeping up to 2048 entries.
- Use `pcre.clear_cache()` when you need to free the cache proactively.
- Non-hashable pattern objects skip the cache and are compiled each time.

## Default flags for text patterns
- String patterns enable `Flag.UTF` and `Flag.UCP` automatically so behaviour matches `re`.
- Byte patterns keep both flags disabled; opt in manually if Unicode semantics are desired.
- Explicitly supply `Flag.NO_UTF`/`Flag.NO_UCP` to override the defaults for strings.

## Additional usage notes
- All top-level helpers (`match`, `search`, `fullmatch`, `finditer`, `findall`) defer to the cached compiler.
- Compiled `Pattern` objects expose `.pattern`, `.pattern_bytes`, `.flags`, and `.groupindex` for introspection.
- Execution helpers accept `pos`, `endpos`, and `options`, allowing you to thread PCRE2 execution flags per call.
