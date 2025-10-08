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
from collections.abc import Callable
from pathlib import Path

from setuptools import Extension, setup


MODULE_SOURCES = [
    "pcre/pcre_ext/pcre2.c",
    "pcre/pcre_ext/error.c",
    "pcre/pcre_ext/cache.c",
    "pcre/pcre_ext/flag.c",
    "pcre/pcre_ext/util.c",
]

LIB_EXTENSIONS = [
    ".so",
    ".so.0",
    ".so.1",
    ".a",
    ".dylib",
    ".sl",
]


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
        prefixes.extend(Path(p) for p in ("/usr", "/usr/local"))
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
    base = "libpcre2-8"
    for extension in LIB_EXTENSIONS:
        if (directory / f"{base}{extension}").exists():
            return True
    return False


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

    env_libs = os.environ.get("PCRE2_LIBRARIES")
    if env_libs:
        for name in env_libs.split(os.pathsep):
            _extend_unique(libraries, name)

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

    if "pcre2-8" not in libraries:
        libraries.append("pcre2-8")

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
