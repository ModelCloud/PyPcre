# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

from __future__ import annotations

import os
import platform
import shlex
import sys
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


ROOT_DIR = Path(__file__).resolve().parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

import setup_utils
from setup_utils import (
    augment_compile_flags,
    compiler_supports_flag,
    compiler_supports_flags,
    discover_include_dirs,
    discover_library_dirs,
    extend_env_paths,
    extend_unique,
    find_library_with_brew,
    find_library_with_ldconfig,
    find_library_with_pkg_config,
    has_header,
    has_library,
    is_solaris_platform,
    is_truthy_env,
    is_windows_platform,
    locate_library_file,
    prepare_pcre2_source,
    run_pkg_config,
)


MODULE_SOURCES = [
    "pcre_ext/pcre2.c",
    "pcre_ext/error.c",
    "pcre_ext/cache.c",
    "pcre_ext/flag.c",
    "pcre_ext/util.c",
    "pcre_ext/memory.c",
]

PCRE_EXT_DIR = ROOT_DIR / "pcre_ext"
PCRE2_REPO_URL = "https://github.com/PCRE2Project/pcre2.git"
PCRE2_TAG = "pcre2-10.46"

# Platform-specific library naming/selection
if os.name == "nt":
    # Only accept MSVC libraries on Windows; never consider .a
    LIB_EXTENSIONS = [".lib"]
    # Prefer the static library when we build PCRE2 ourselves; fall back to import lib
    LIBRARY_BASENAME = "pcre2-8-static"
    LIBRARY_SEARCH_PATTERNS = [
        "**/pcre2-8-static.lib",
        "**/pcre2-8.lib",
        "**/pcre2-8.dll",  # runtime (for packaging/copying), not for linking
    ]
else:
    LIB_EXTENSIONS = [
        ".so",
        ".so.0",
        ".so.1",
        ".a",
        ".dylib",
        ".sl",
    ]
    LIBRARY_BASENAME = "libpcre2-8"
    LIBRARY_SEARCH_PATTERNS = [
        f"**/{LIBRARY_BASENAME}.a",
        f"**/{LIBRARY_BASENAME}.so",
        f"**/{LIBRARY_BASENAME}.so.*",
        f"**/{LIBRARY_BASENAME}.dylib",
    ]

RUNTIME_LIBRARY_FILES: list[str] = []

setup_utils.configure_environment(
    pcre_ext_dir=PCRE_EXT_DIR,
    repo_url=PCRE2_REPO_URL,
    repo_tag=PCRE2_TAG,
    lib_extensions=LIB_EXTENSIONS,
    library_basename=LIBRARY_BASENAME,
    library_search_patterns=LIBRARY_SEARCH_PATTERNS,
)

if is_solaris_platform():
    os.environ["PYPCRE_BUILD_FROM_SOURCE"] = "1"


def filter_with_real_path(paths: list[str]) -> list[str]:
    unique_libs = []
    seen_realpaths = set()
    for path in paths:
        real = os.path.realpath(path)
        if real not in seen_realpaths:
            seen_realpaths.add(real)
            unique_libs.append(path)
    return unique_libs


def collect_build_config() -> dict[str, list[str] | list[tuple[str, str | None]]]:
    include_dirs: list[str] = []
    library_dirs: list[str] = []
    libraries: list[str] = []
    extra_compile_args: list[str] = []
    extra_link_args: list[str] = []
    define_macros: list[tuple[str, str | None]] = []
    library_files: list[str] = []

    source_include_dirs, source_library_dirs, source_library_files = prepare_pcre2_source()
    for directory in source_include_dirs:
        extend_unique(include_dirs, directory)
    for directory in source_library_dirs:
        extend_unique(library_dirs, directory)
    for path in source_library_files:
        extend_unique(library_files, path)

    cflags = run_pkg_config("--cflags")
    libs = run_pkg_config("--libs")

    for flag in cflags:
        if flag.startswith("-I") and len(flag) > 2:
            extend_unique(include_dirs, flag[2:])
        elif flag.startswith("-D") and len(flag) > 2:
            name_value = flag[2:].split("=", 1)
            define_macros.append((name_value[0], name_value[1] if len(name_value) > 1 else None))
        else:
            extra_compile_args.append(flag)

    for flag in libs:
        if flag.startswith("-L") and len(flag) > 2:
            extend_unique(library_dirs, flag[2:])
        elif flag.startswith("-l") and len(flag) > 2:
            extend_unique(libraries, flag[2:])
        else:
            extra_link_args.append(flag)

    extend_env_paths(include_dirs, "PYPCRE_INCLUDE_DIR")
    extend_env_paths(library_dirs, "PYPCRE_LIBRARY_DIR")

    env_lib_path = os.environ.get("PYPCRE_LIBRARY_PATH")
    if env_lib_path:
        for raw_path in env_lib_path.split(os.pathsep):
            candidate = raw_path.strip()
            if not candidate:
                continue
            path = Path(candidate)
            if path.is_file() or any(candidate.endswith(ext) for ext in LIB_EXTENSIONS):
                extend_unique(library_files, str(path))
                parent = str(path.parent)
                if parent:
                    extend_unique(library_dirs, parent)
            else:
                extend_unique(library_dirs, candidate)

    extend_env_paths(libraries, "PYPCRE_LIBRARIES")

    directory_candidates = [Path(p) for p in library_dirs]
    directory_candidates.extend(Path(p) for p in discover_library_dirs())
    for directory in directory_candidates:
        located = locate_library_file(directory)
        if located is not None:
            extend_unique(library_files, str(located))

    for path in find_library_with_pkg_config():
        extend_unique(library_files, path)

    for path in find_library_with_ldconfig():
        extend_unique(library_files, path)

    for path in find_library_with_brew():
        extend_unique(library_files, path)

    env_cflags = os.environ.get("PYPCRE_CFLAGS")
    if env_cflags:
        extra_compile_args.extend(shlex.split(env_cflags))

    env_ldflags = os.environ.get("PYPCRE_LDFLAGS")
    if env_ldflags:
        extra_link_args.extend(shlex.split(env_ldflags))

    has_std_flag = any(
        flag.lower().startswith("/std:") or flag.startswith("-std=") for flag in extra_compile_args
    )
    has_msvc_atomics_flag = any(flag.lower() == "/experimental:c11atomics" for flag in extra_compile_args)
    if is_windows_platform():
        if not has_std_flag:
            c11_probe = (
                "#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L\n"
                "#error C11 support required\n"
                "#endif\n"
                "int main(void) {\n"
                "    int value = 1;\n"
                "    return _Generic(value, int: 0, default: 1);\n"
                "}\n"
            )
            if compiler_supports_flags(["/std:c11"], code=c11_probe):
                extra_compile_args.append("/std:c11")
            elif compiler_supports_flags(["/std:clatest"], code=c11_probe):
                extra_compile_args.append("/std:clatest")
            else:
                raise RuntimeError("MSVC requires /std:c11 or newer for atomics support")
        if not has_msvc_atomics_flag and compiler_supports_flag("/experimental:c11atomics"):
            extra_compile_args.append("/experimental:c11atomics")
    elif not has_std_flag:
        if compiler_supports_flag("-std=c11"):
            extra_compile_args.append("-std=c11")
        else:
            extra_compile_args.append("-std=c99")

    if not has_header(include_dirs):
        include_dirs.extend(discover_include_dirs())

    if not has_library(library_dirs):
        library_dirs.extend(discover_library_dirs())

    runtime_libraries: list[str] = []

    if library_files:
        for runtime_path in library_files:
            lower_name = runtime_path.lower()
            if lower_name.endswith(".dll") or lower_name.endswith(".dylib") or ".so" in Path(runtime_path).name:
                extend_unique(runtime_libraries, runtime_path)

        linkable_files: list[str] = []
        for path in library_files:
            suffix = Path(path).suffix.lower()
            if suffix == ".dll":
                continue
            linkable_files.append(path)

        if linkable_files:
            libraries = [lib for lib in libraries if lib != "pcre2-8"]
            for path in linkable_files:
                extend_unique(extra_link_args, path)
                parent = str(Path(path).parent)
                if parent:
                    extend_unique(library_dirs, parent)
        elif "pcre2-8" not in libraries:
            libraries.append("pcre2-8")
    elif "pcre2-8" not in libraries:
        libraries.append("pcre2-8")

    if sys.platform.startswith("linux") and "dl" not in libraries:
        libraries.append("dl")

    if is_windows_platform():
        has_runtime_dll = any(path.lower().endswith(".dll") for path in runtime_libraries)
        force_static_env = is_truthy_env("PYPCRE_FORCE_STATIC")
        if (force_static_env or (library_files and not has_runtime_dll)) and not any(
            macro[0] == "PCRE2_STATIC" for macro in define_macros
        ):
            define_macros.append(("PCRE2_STATIC", "1"))

    augment_compile_flags(extra_compile_args)

    RUNTIME_LIBRARY_FILES.clear()
    RUNTIME_LIBRARY_FILES.extend(runtime_libraries)

    if is_solaris_platform() and platform.architecture()[0] == "64bit":
        extra_link_args_x64 = [path for path in extra_link_args if '64' in path]
        if extra_link_args_x64:
            extra_link_args = extra_link_args_x64

    # On Windows, only keep .lib paths in extra_link_args (drop accidental .a)
    if os.name == "nt":
        extra_link_args = [p for p in extra_link_args if p.lower().endswith(".lib")]

    extra_link_args = filter_with_real_path(extra_link_args)
    config = {
        "include_dirs": include_dirs,
        "library_dirs": library_dirs,
        "libraries": libraries,
        "extra_compile_args": extra_compile_args,
        "extra_link_args": extra_link_args,
        "define_macros": define_macros,
    }

    print(f"build config: {config}")

    return config


EXTENSION = Extension(
    name="pcre_ext_c",
    sources=MODULE_SOURCES,
    **collect_build_config(),
)

setup(ext_modules=[EXTENSION], cmdclass={"build_ext": build_ext})
