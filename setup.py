# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

from __future__ import annotations

import os
import platform
import shlex
import subprocess
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

from setuptools import Extension, setup

try:
    from setuptools._distutils.ccompiler import CCompiler, new_compiler
    from setuptools._distutils.errors import CCompilerError, DistutilsExecError
    from setuptools._distutils.sysconfig import customize_compiler
except ImportError:  # pragma: no cover - fallback for older Python environments
    from distutils.ccompiler import CCompiler, new_compiler  # type: ignore
    from distutils.errors import CCompilerError, DistutilsExecError  # type: ignore
    from distutils.sysconfig import customize_compiler  # type: ignore


MODULE_SOURCES = [
    "pcre_ext/pcre2.c",
    "pcre_ext/error.c",
    "pcre_ext/cache.c",
    "pcre_ext/flag.c",
    "pcre_ext/util.c",
    "pcre_ext/memory.c",
]

LIB_EXTENSIONS = [
    ".so",
    ".so.0",
    ".so.1",
    ".a",
    ".dylib",
    ".sl",
]

LIBRARY_BASENAME = "libpcre2-8"


def _run_pkg_config(*args: str) -> list[str]:
    try:
        result = subprocess.run(
            ["pkg-config", *args, "libpcre2-8"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return []
    return shlex.split(result.stdout.strip())


def _run_pkg_config_var(argument: str) -> str | None:
    try:
        result = subprocess.run(
            ["pkg-config", argument, "libpcre2-8"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None
    return result.stdout.strip() or None


def _run_command(command: list[str]) -> str | None:
    try:
        result = subprocess.run(
            command,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None
    return result.stdout.strip() or None


_COMPILER_INITIALIZED = False
_COMPILER_INSTANCE: CCompiler | None = None
_COMPILER_FLAG_CACHE: dict[str, bool] = {}
_TRUTHY_VALUES = {"1", "true", "yes", "on"}


def _is_truthy_env(name: str) -> bool:
    value = os.environ.get(name)
    if value is None:
        return False
    return value.strip().lower() in _TRUTHY_VALUES


def _get_test_compiler() -> CCompiler | None:
    global _COMPILER_INITIALIZED, _COMPILER_INSTANCE
    if _COMPILER_INITIALIZED:
        return _COMPILER_INSTANCE
    _COMPILER_INITIALIZED = True
    try:
        compiler = new_compiler()
        customize_compiler(compiler)
    except Exception:
        _COMPILER_INSTANCE = None
    else:
        _COMPILER_INSTANCE = compiler
    return _COMPILER_INSTANCE


def _extract_macos_architectures(command: list[str] | tuple[str, ...] | None) -> list[str]:
    if not isinstance(command, (list, tuple)):
        return []
    arches: list[str] = []
    iterator = iter(command)
    for token in iterator:
        if token != "-arch":
            continue
        arch = next(iterator, "")
        if arch:
            arches.append(arch)
    return arches


def _macos_compiler_architectures(compiler: CCompiler | None) -> set[str]:
    arches: set[str] = set()
    if compiler is not None:
        for attr in ("compiler", "compiler_so", "compiler_cxx", "linker_so"):
            arches.update(_extract_macos_architectures(getattr(compiler, attr, None)))
    archflags = os.environ.get("ARCHFLAGS")
    if archflags:
        arches.update(_extract_macos_architectures(tuple(shlex.split(archflags))))
    for env_name in ("CFLAGS", "CPPFLAGS"):
        value = os.environ.get(env_name)
        if value:
            arches.update(_extract_macos_architectures(tuple(shlex.split(value))))
    return {arch for arch in arches if arch}


def _is_x86_architecture(arch: str) -> bool:
    normalized = arch.lower()
    return normalized in {"x86_64", "x86_64h", "i386", "i486", "i586", "i686", "amd64"}


def _should_disable_native_flags_for_macos(compiler: CCompiler | None) -> bool:
    if sys.platform != "darwin":
        return False
    arches = _macos_compiler_architectures(compiler)
    if not arches:
        machine = platform.machine()
        if machine:
            arches.add(machine)
    if not arches:
        return False
    if len(arches) > 1:
        return True
    arch = next(iter(arches))
    return not _is_x86_architecture(arch)


def _compiler_supports_flag(flag: str) -> bool:
    cached = _COMPILER_FLAG_CACHE.get(flag)
    if cached is not None:
        return cached

    compiler = _get_test_compiler()
    if compiler is None:
        _COMPILER_FLAG_CACHE[flag] = False
        return False

    with tempfile.TemporaryDirectory() as tmpdir:
        source = Path(tmpdir) / "flag_check.c"
        source.write_text("int main(void) { return 0; }\n", encoding="utf-8")
        try:
            compiler.compile(
                [str(source)],
                output_dir=tmpdir,
                extra_postargs=[flag],
            )
        except (CCompilerError, DistutilsExecError, OSError):
            _COMPILER_FLAG_CACHE[flag] = False
        else:
            _COMPILER_FLAG_CACHE[flag] = True
    return _COMPILER_FLAG_CACHE[flag]


def _augment_compile_flags(flags: list[str]) -> None:
    if _is_truthy_env("PCRE2_DISABLE_OPT_FLAGS"):
        return

    disable_native = _is_truthy_env("PCRE2_DISABLE_NATIVE_FLAGS")
    compiler = _get_test_compiler()
    if not disable_native and _should_disable_native_flags_for_macos(compiler):
        # Apple universal builds (arm64 + x86_64) and arm64-only builds reject x86 specific flags.
        disable_native = True
    candidate_flags: list[tuple[str, bool]] = [
        ("-O3", False),
        ("-march=native", True),
        ("-mtune=native", True),
        ("-fomit-frame-pointer", False),
        ("-funroll-loops", False),
        #("-falign-loops=32", False),
    ]

    seen = set(flags)
    for flag, requires_native in candidate_flags:
        if requires_native and disable_native:
            continue
        if flag in seen:
            continue
        if not _compiler_supports_flag(flag):
            continue
        flags.append(flag)
        seen.add(flag)


def _homebrew_prefixes() -> list[Path]:
    if sys.platform != "darwin":
        return []

    prefixes: list[Path] = []
    for args in (["brew", "--prefix", "pcre2"], ["brew", "--prefix"]):
        output = _run_command(args)
        if not output:
            continue
        path = Path(output)
        if path.exists():
            prefixes.append(path)
    return prefixes


def _linux_multiarch_dirs() -> list[str]:
    arch = platform.machine()
    mapping = {
        "x86_64": ["x86_64-linux-gnu"],
        "aarch64": ["aarch64-linux-gnu"],
        "arm64": ["aarch64-linux-gnu"],
        "armv7l": ["arm-linux-gnueabihf"],
        "armv6l": ["arm-linux-gnueabihf"],
        "armv8l": ["arm-linux-gnueabihf"],
        "i686": ["i386-linux-gnu"],
        "i386": ["i386-linux-gnu"],
        "ppc64le": ["powerpc64le-linux-gnu"],
        "s390x": ["s390x-linux-gnu"],
    }
    return mapping.get(arch, [])


def _platform_prefixes() -> list[Path]:
    prefixes: list[Path] = []

    env_root = os.environ.get("PCRE2_ROOT")
    if env_root:
        for value in env_root.split(os.pathsep):
            path = Path(value)
            if path.exists():
                prefixes.append(path)

    if sys.platform.startswith("linux"):
        prefixes.extend(Path(p) for p in ("/usr/local", "/usr"))
    elif sys.platform == "darwin":
        prefixes.extend(_homebrew_prefixes())
        prefixes.extend(Path(p) for p in ("/opt/homebrew", "/usr/local", "/usr"))
    elif sys.platform.startswith("freebsd"):
        prefixes.extend(Path(p) for p in ("/usr/local", "/usr"))
    elif sys.platform.startswith("sunos") or sys.platform.startswith("solaris"):
        prefixes.extend(Path(p) for p in ("/usr", "/usr/local", "/opt/local"))
    else:
        prefixes.extend(Path(p) for p in ("/usr/local", "/usr"))

    seen: set[Path] = set()
    ordered: list[Path] = []
    for prefix in prefixes:
        if prefix not in seen:
            ordered.append(prefix)
            seen.add(prefix)
    return ordered


def _platform_library_subdirs() -> list[str]:
    subdirs = ["lib", "lib64", "lib32", "lib/pcre2"]

    if sys.platform.startswith("linux"):
        for multiarch in _linux_multiarch_dirs():
            subdirs.append(f"lib/{multiarch}")
        subdirs.extend([
            "lib/x86_64-linux-gnu",
            "lib/i386-linux-gnu",
            "lib/aarch64-linux-gnu",
            "lib/arm-linux-gnueabihf",
        ])
    elif sys.platform.startswith("sunos") or sys.platform.startswith("solaris"):
        subdirs.extend(["lib/64", "lib/amd64"])

    seen: set[str] = set()
    ordered: list[str] = []
    for subdir in subdirs:
        if subdir not in seen:
            ordered.append(subdir)
            seen.add(subdir)
    return ordered


def _extend_unique(target: list[str], value: str) -> None:
    if value and value not in target:
        target.append(value)


def _extend_with_existing(
    target: list[str],
    candidates: list[Path],
    predicate: Callable[[Path], bool] | None = None,
) -> None:
    for candidate in candidates:
        if not candidate.is_dir():
            continue
        if predicate is not None and not predicate(candidate):
            continue
        _extend_unique(target, str(candidate))


def _header_exists(directory: Path) -> bool:
    return (directory / "pcre2.h").exists()


def _library_exists(directory: Path) -> bool:
    return _locate_library_file(directory) is not None


def _locate_library_file(directory: Path) -> Path | None:
    if not directory.exists():
        return None
    for extension in LIB_EXTENSIONS:
        candidate = directory / f"{LIBRARY_BASENAME}{extension}"
        if candidate.exists():
            return candidate
    for candidate in directory.glob(f"{LIBRARY_BASENAME}.so.*"):
        if candidate.exists():
            return candidate
    fallback = directory / f"{LIBRARY_BASENAME}.dll"
    if fallback.exists():
        return fallback
    return None


def _find_library_with_pkg_config() -> list[str]:
    library_files: list[str] = []
    libfile = _run_pkg_config_var("--variable=libfile")
    if libfile:
        path = Path(libfile)
        if path.exists():
            library_files.append(str(path))
    if not library_files:
        libdir = _run_pkg_config_var("--variable=libdir")
        if libdir:
            candidate = _locate_library_file(Path(libdir))
            if candidate is not None:
                library_files.append(str(candidate))
    return library_files


def _find_library_with_ldconfig() -> list[str]:
    if not sys.platform.startswith("linux"):
        return []
    output = _run_command(["ldconfig", "-p"])
    if not output:
        return []
    for line in output.splitlines():
        if "libpcre2-8.so" not in line:
            continue
        parts = line.strip().split(" => ")
        if len(parts) != 2:
            continue
        path = Path(parts[1].strip())
        if path.exists():
            return [str(path)]
    return []


def _find_library_with_brew() -> list[str]:
    if sys.platform != "darwin":
        return []
    library_files: list[str] = []
    for prefix in _homebrew_prefixes():
        candidate = _locate_library_file(prefix / "lib")
        if candidate is not None:
            library_files.append(str(candidate))
    return library_files


def _discover_include_dirs() -> list[str]:
    prefixes = _platform_prefixes()
    candidates: list[Path] = []
    for prefix in prefixes:
        candidates.extend(
            [
                prefix / "include",
                prefix / "include/pcre2",
            ]
        )
    include_dirs: list[str] = []
    _extend_with_existing(include_dirs, candidates, _header_exists)
    return include_dirs


def _discover_library_dirs() -> list[str]:
    prefixes = _platform_prefixes()
    candidates: list[Path] = []
    subdirs = _platform_library_subdirs()
    for prefix in prefixes:
        for subdir in subdirs:
            candidates.append(prefix / subdir)
    library_dirs: list[str] = []
    _extend_with_existing(library_dirs, candidates, _library_exists)
    return library_dirs


def _has_header(include_dirs: list[str]) -> bool:
    for directory in include_dirs:
        if _header_exists(Path(directory)):
            return True
    return False


def _has_library(library_dirs: list[str]) -> bool:
    for directory in library_dirs:
        if _library_exists(Path(directory)):
            return True
    return False


def _collect_build_config() -> dict[str, list[str] | list[tuple[str, str | None]]]:
    include_dirs: list[str] = []
    library_dirs: list[str] = []
    libraries: list[str] = []
    extra_compile_args: list[str] = []
    extra_link_args: list[str] = []
    define_macros: list[tuple[str, str | None]] = []
    library_files: list[str] = []

    cflags = _run_pkg_config("--cflags")
    libs = _run_pkg_config("--libs")

    for flag in cflags:
        if flag.startswith("-I") and len(flag) > 2:
            _extend_unique(include_dirs, flag[2:])
        elif flag.startswith("-D") and len(flag) > 2:
            name_value = flag[2:].split("=", 1)
            define_macros.append((name_value[0], name_value[1] if len(name_value) > 1 else None))
        else:
            extra_compile_args.append(flag)

    for flag in libs:
        if flag.startswith("-L") and len(flag) > 2:
            _extend_unique(library_dirs, flag[2:])
        elif flag.startswith("-l") and len(flag) > 2:
            _extend_unique(libraries, flag[2:])
        else:
            extra_link_args.append(flag)

    env_include = os.environ.get("PCRE2_INCLUDE_DIR")
    if env_include:
        for path in env_include.split(os.pathsep):
            _extend_unique(include_dirs, path)

    env_lib = os.environ.get("PCRE2_LIBRARY_DIR")
    if env_lib:
        for path in env_lib.split(os.pathsep):
            _extend_unique(library_dirs, path)

    env_lib_path = os.environ.get("PCRE2_LIBRARY_PATH")
    if env_lib_path:
        for raw_path in env_lib_path.split(os.pathsep):
            candidate = raw_path.strip()
            if not candidate:
                continue
            path = Path(candidate)
            if path.is_file() or any(candidate.endswith(ext) for ext in LIB_EXTENSIONS):
                _extend_unique(library_files, str(path))
                parent = str(path.parent)
                if parent:
                    _extend_unique(library_dirs, parent)
            else:
                _extend_unique(library_dirs, candidate)

    env_libs = os.environ.get("PCRE2_LIBRARIES")
    if env_libs:
        for name in env_libs.split(os.pathsep):
            _extend_unique(libraries, name)

    if not library_files:
        for path in _find_library_with_pkg_config():
            _extend_unique(library_files, path)

    if not library_files:
        directory_candidates = [Path(p) for p in library_dirs]
        directory_candidates.extend(Path(p) for p in _discover_library_dirs())
        for directory in directory_candidates:
            located = _locate_library_file(directory)
            if located is not None:
                _extend_unique(library_files, str(located))
                break

    if not library_files:
        for path in _find_library_with_ldconfig():
            _extend_unique(library_files, path)

    if not library_files:
        for path in _find_library_with_brew():
            _extend_unique(library_files, path)

    env_cflags = os.environ.get("PCRE2_CFLAGS")
    if env_cflags:
        extra_compile_args.extend(shlex.split(env_cflags))

    env_ldflags = os.environ.get("PCRE2_LDFLAGS")
    if env_ldflags:
        extra_link_args.extend(shlex.split(env_ldflags))

    if not any(flag.startswith("-std=") for flag in extra_compile_args):
        extra_compile_args.append("-std=c99")

    if not _has_header(include_dirs):
        include_dirs.extend(_discover_include_dirs())

    if not _has_library(library_dirs):
        library_dirs.extend(_discover_library_dirs())

    if library_files:
        libraries = [lib for lib in libraries if lib != "pcre2-8"]
        for path in library_files:
            _extend_unique(extra_link_args, path)
            parent = str(Path(path).parent)
            if parent:
                _extend_unique(library_dirs, parent)
    elif "pcre2-8" not in libraries:
        libraries.append("pcre2-8")

    if sys.platform.startswith("linux") and "dl" not in libraries:
        libraries.append("dl")

    _augment_compile_flags(extra_compile_args)
    print(extra_compile_args)

    return {
        "include_dirs": include_dirs,
        "library_dirs": library_dirs,
        "libraries": libraries,
        "extra_compile_args": extra_compile_args,
        "extra_link_args": extra_link_args,
        "define_macros": define_macros,
    }


EXTENSION = Extension(
    name="pcre.cpcre2",
    sources=MODULE_SOURCES,
    **_collect_build_config(),
)


setup(ext_modules=[EXTENSION])
