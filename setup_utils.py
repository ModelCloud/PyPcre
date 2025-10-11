# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

from __future__ import annotations

import os
import platform
import shlex
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable, Iterable
from pathlib import Path


from setuptools._distutils.ccompiler import CCompiler, new_compiler
from setuptools._distutils.errors import CCompilerError, DistutilsExecError
from setuptools._distutils.sysconfig import customize_compiler


_PCRE_EXT_DIR: Path | None = None
_PCRE2_REPO_URL: str | None = None
_PCRE2_TAG: str | None = None


_LIB_EXTENSIONS: tuple[str, ...] = ()
_LIBRARY_BASENAME: str | None = None
_LIBRARY_SEARCH_PATTERNS: tuple[str, ...] = ()


def _ensure_macos_archflags() -> None:
    if sys.platform != "darwin":
        return
    if os.environ.get("ARCHFLAGS"):
        return
    machine = platform.machine()
    if not machine:
        return
    normalized = machine.lower()
    if normalized == "aarch64":
        normalized = "arm64"
    os.environ["ARCHFLAGS"] = f"-arch {normalized}"


def configure_environment(
    *,
    pcre_ext_dir: Path,
    repo_url: str,
    repo_tag: str,
    lib_extensions: Iterable[str],
    library_basename: str,
    library_search_patterns: Iterable[str],
) -> None:
    """Inject project-specific constants from setup.py."""

    global _PCRE_EXT_DIR, _PCRE2_REPO_URL, _PCRE2_TAG
    global _LIB_EXTENSIONS, _LIBRARY_BASENAME, _LIBRARY_SEARCH_PATTERNS

    _PCRE_EXT_DIR = pcre_ext_dir
    _PCRE2_REPO_URL = repo_url
    _PCRE2_TAG = repo_tag
    _LIB_EXTENSIONS = tuple(lib_extensions)
    _LIBRARY_BASENAME = library_basename
    _LIBRARY_SEARCH_PATTERNS = tuple(library_search_patterns)

    _ensure_macos_archflags()


def _require_config(value: object, name: str) -> object:
    if value is None or (isinstance(value, tuple) and not value):
        raise RuntimeError(
            f"setup_utils.configure_environment() must be called before accessing {name}"
        )
    return value


def _get_pcre_ext_dir() -> Path:
    return _require_config(_PCRE_EXT_DIR, "PCRE_EXT_DIR")  # type: ignore[return-value]


def _get_repo_url() -> str:
    return _require_config(_PCRE2_REPO_URL, "PCRE2_REPO_URL")  # type: ignore[return-value]


def _get_repo_tag() -> str:
    return _require_config(_PCRE2_TAG, "PCRE2_TAG")  # type: ignore[return-value]


def _get_lib_extensions() -> tuple[str, ...]:
    return _require_config(_LIB_EXTENSIONS, "LIB_EXTENSIONS")  # type: ignore[return-value]


def _get_library_basename() -> str:
    return _require_config(_LIBRARY_BASENAME, "LIBRARY_BASENAME")  # type: ignore[return-value]


def _get_library_search_patterns() -> tuple[str, ...]:
    return _require_config(_LIBRARY_SEARCH_PATTERNS, "LIBRARY_SEARCH_PATTERNS")  # type: ignore[return-value]


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


def _log_cmake_validation_failure(path: str, error: subprocess.CalledProcessError) -> None:
    message_parts = [
        f"Detected CMake at {path} but `cmake --version` exited with {error.returncode}.",
    ]
    if error.stdout:
        message_parts.append("stdout:\n" + error.stdout.rstrip())
    if error.stderr:
        message_parts.append("stderr:\n" + error.stderr.rstrip())
    sys.stderr.write("\n".join(message_parts) + "\n")


def _path_contains_pyenv_shims(path: str) -> bool:
    normalized = path.replace("\\", "/").lower()
    return ".pyenv" in normalized and "/shims/" in normalized


def _is_python_script(path: str) -> bool:
    try:
        with open(path, "rb") as handle:
            first_line = handle.readline().strip().lower()
    except OSError:
        return False
    if not first_line.startswith(b"#!"):
        return False
    return b"python" in first_line


def _is_pyenv_python_shim(path: str) -> bool:
    return _path_contains_pyenv_shims(path) and _is_python_script(path)


def _log_skipping_pyenv_shim(path: str) -> None:
    sys.stderr.write(f"Ignoring pyenv CMake shim at {path}\n")


def _path_without_pyenv_shims() -> str | None:
    raw_path = os.environ.get("PATH")
    if not raw_path:
        return None
    entries: list[str] = []
    for part in raw_path.split(os.pathsep):
        candidate = part.strip()
        if not candidate:
            continue
        if _path_contains_pyenv_shims(candidate):
            continue
        entries.append(candidate)
    if not entries:
        return None
    return os.pathsep.join(entries)


def _candidate_extensions() -> list[str]:
    if os.name != "nt":
        return [""]
    pathext = os.environ.get("PATHEXT")
    if not pathext:
        return ["", ".exe", ".bat", ".cmd", ".com"]
    extensions = [""]
    for ext in pathext.split(os.pathsep):
        normalized = ext.strip()
        if not normalized:
            continue
        if not normalized.startswith("."):
            normalized = f".{normalized}"
        extensions.append(normalized.lower())
    return extensions


def _executable_candidates_from_path(command_names: list[str], search_path: str | None) -> list[str]:
    if not search_path:
        return []
    candidates: list[str] = []
    extensions = _candidate_extensions()
    for directory in search_path.split(os.pathsep):
        directory = directory.strip()
        if not directory:
            continue
        try:
            base = Path(directory)
        except Exception:
            continue
        for name in command_names:
            for extension in extensions:
                candidate = base / (name + extension)
                try:
                    if candidate.is_file() and os.access(candidate, os.X_OK):
                        try:
                            resolved = candidate.resolve()
                        except OSError:
                            resolved = candidate
                        candidates.append(str(resolved))
                except OSError:
                    continue
    return candidates


def _filter_existing_executables(raw_paths: Iterable[str]) -> list[str]:
    filtered: list[str] = []
    for path in raw_paths:
        candidate = Path(path)
        try:
            if candidate.is_file() and os.access(candidate, os.X_OK):
                try:
                    filtered.append(str(candidate.resolve()))
                except OSError:
                    filtered.append(str(candidate))
        except OSError:
            continue
    return filtered


def _executable_candidates_from_system_tools(command_names: list[str]) -> list[str]:
    candidates: list[str] = []
    system = platform.system().lower()
    has_which = shutil.which("which") is not None
    for name in command_names:
        if system == "windows":
            where_command = shutil.which("where")
            if where_command:
                try:
                    result = subprocess.run(
                        [where_command, name],
                        check=True,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        text=True,
                    )
                except (FileNotFoundError, subprocess.CalledProcessError):
                    pass
                else:
                    raw = [line.strip() for line in result.stdout.splitlines()]
                    candidates.extend(_filter_existing_executables(raw))
            continue

        # POSIX-like systems
        if has_which:
            try:
                result = subprocess.run(
                    ["which", "-a", name],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
            except (FileNotFoundError, subprocess.CalledProcessError):
                pass
            else:
                raw = [line.strip() for line in result.stdout.splitlines()]
                candidates.extend(_filter_existing_executables(raw))

        if system in {"linux", "freebsd"} and shutil.which("whereis"):
            try:
                result = subprocess.run(
                    ["whereis", "-b", name],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
            except (FileNotFoundError, subprocess.CalledProcessError):
                continue
            raw_paths: list[str] = []
            output = result.stdout.strip()
            if output:
                parts = output.split()
                raw_paths.extend(part.strip().rstrip(":") for part in parts[1:])
            candidates.extend(_filter_existing_executables(raw_paths))

    return candidates


def _deduplicate_preserve_order(items: list[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for item in items:
        if not item or item in seen:
            continue
        seen.add(item)
        result.append(item)
    return result


def _resolve_cmake_executable() -> str | None:
    explicit = os.environ.get("CMAKE_EXECUTABLE")
    command_names = ["cmake", "cmake3"]
    candidate_paths: list[str] = []
    if explicit:
        candidate_paths.append(explicit)
    candidate_paths.extend(_executable_candidates_from_path(command_names, os.environ.get("PATH")))
    filtered_path = _path_without_pyenv_shims()
    if filtered_path:
        candidate_paths.extend(_executable_candidates_from_path(command_names, filtered_path))
    candidate_paths.extend(_executable_candidates_from_system_tools(command_names))

    candidate_paths = _deduplicate_preserve_order(candidate_paths)
    if candidate_paths:
        listing = "\n".join(f"  - {path}" for path in candidate_paths)
        sys.stderr.write(f"Found CMake candidates:\n{listing}\n")

    for path in candidate_paths:
        if _is_pyenv_python_shim(path):
            _log_skipping_pyenv_shim(path)
            continue
        try:
            result = subprocess.run(
                [path, "--version"],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        except FileNotFoundError:
            continue
        except subprocess.CalledProcessError as exc:
            _log_cmake_validation_failure(path, exc)
            continue
        sys.stderr.write(f"Validated CMake executable at {path}\n")
        version_line = (result.stdout or "").strip().splitlines()[0] if result.stdout else ""
        print(f"PyPcre build: using CMake executable at {path}{f' ({version_line})' if version_line else ''}")
        return path
    return None


_COMPILER_INITIALIZED = False
_COMPILER_INSTANCE: CCompiler | None = None
_COMPILER_FLAG_CACHE: dict[str, bool] = {}
_COMPILER_FLAG_COMBO_CACHE: dict[tuple[tuple[str, ...], str], bool] = {}
_TRUTHY_VALUES = {"1", "true", "yes", "on"}


def _is_truthy_env(name: str) -> bool:
    value = os.environ.get(name)
    if value is None:
        return False
    return value.strip().lower() in _TRUTHY_VALUES


def _is_windows_platform() -> bool:
    return sys.platform.startswith("win") or os.name == "nt"


def _is_solaris_platform() -> bool:
    platform_name = sys.platform.lower()
    return platform_name.startswith("sunos") or platform_name.startswith("solaris")


def _is_wsl_environment() -> bool:
    if not sys.platform.startswith("linux"):
        return False
    if os.environ.get("WSL_DISTRO_NAME"):
        return True
    try:
        release = platform.release()
    except Exception:
        return False
    return "microsoft" in release.lower()


def _clean_previous_build(destination: Path, build_dir: Path, build_roots: list[Path]) -> None:
    if not destination.exists():
        return

    env = os.environ.copy()
    cleaned = False

    if build_dir.exists():
        cmake_cache = build_dir / "CMakeCache.txt"
        cmake_executable = shutil.which("cmake")
        if cmake_cache.exists() and cmake_executable:
            cmake_command = ["cmake", "--build", str(build_dir), "--target", "clean"]
            if _is_windows_platform():
                cmake_command.extend(["--config", "Release"])
            try:
                subprocess.run(cmake_command, cwd=destination, env=env, check=True)
            except subprocess.CalledProcessError:
                pass
            else:
                cleaned = True

        makefile_exists = any((build_dir / name).exists() for name in ("Makefile", "makefile"))
        if makefile_exists and shutil.which("make"):
            for target in ("clean", "distclean"):
                try:
                    subprocess.run(["make", target], cwd=build_dir, env=env, check=True)
                except subprocess.CalledProcessError:
                    continue
                else:
                    cleaned = True
                    break

    if not cleaned:
        for candidate in (build_dir, destination / ".libs", destination / "src" / ".libs"):
            if candidate.is_dir():
                shutil.rmtree(candidate, ignore_errors=True)

    patterns = _get_library_search_patterns()

    for root in build_roots:
        if not root.exists():
            continue
        for pattern in patterns:
            for path in root.glob(pattern):
                try:
                    path.unlink()
                except IsADirectoryError:
                    shutil.rmtree(path, ignore_errors=True)
                except FileNotFoundError:
                    continue


def _prepare_pcre2_source() -> tuple[list[str], list[str], list[str]]:
    if (_is_windows_platform() and not _is_wsl_environment()) or _is_solaris_platform():
        os.environ["PYPCRE_BUILD_FROM_SOURCE"] = "1"

    if not _is_truthy_env("PYPCRE_BUILD_FROM_SOURCE"):
        return ([], [], [])

    destination = _get_pcre_ext_dir() / _get_repo_tag()
    git_dir = destination / ".git"
    repo_already_present = destination.exists()

    if destination.exists() and not git_dir.is_dir():
        raise RuntimeError(
            f"Existing directory {destination} is not a git checkout; remove or rename it before building"
        )

    if not destination.exists():
        clone_command = [
            "git",
            "clone",
            "--depth",
            "1",
            "--branch",
            _get_repo_tag(),
            "--recurse-submodules",
            "--shallow-submodules",
            _get_repo_url(),
            str(destination),
        ]
        try:
            subprocess.run(clone_command, check=True)
        except FileNotFoundError as exc:  # pragma: no cover - git missing on build host
            raise RuntimeError("git is required to fetch PCRE2 sources when PYPCRE_BUILD_FROM_SOURCE=1") from exc
        except subprocess.CalledProcessError as exc:
            raise RuntimeError(
                "Failed to clone PCRE2 source from official repository; see the output above for details"
            ) from exc

    try:
        subprocess.run(
            ["git", "submodule", "update", "--init", "--recursive"],
            cwd=destination,
            check=True,
        )
    except FileNotFoundError as exc:  # pragma: no cover - git missing on build host
        raise RuntimeError("git with submodule support is required to fetch PCRE2 dependencies") from exc
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(
            "Failed to update PCRE2 git submodules; see the output above for details"
        ) from exc

    build_dir = destination / "build"
    build_roots = [
        destination,
        destination / ".libs",
        destination / "src",
        destination / "src" / ".libs",
        build_dir,
        build_dir / "lib",
        build_dir / "bin",
        build_dir / "Release",
        build_dir / "Debug",
        build_dir / "RelWithDebInfo",
        build_dir / "MinSizeRel",
    ]

    if repo_already_present:
        _clean_previous_build(destination, build_dir, build_roots)

    def _has_built_library() -> bool:
        patterns = [
            # POSIX/Mac
            "libpcre2-8.so",
            "libpcre2-8.so.*",
            "libpcre2-8.a",
            "libpcre2-8.dylib",
            # Windows (MSVC)
            "pcre2-8.lib",
            "pcre2-8-static.lib",
            "pcre2-8.dll",
        ]
        for root in build_roots:
            if not root.exists():
                continue
            for pattern in patterns:
                if any(root.glob(f"**/{pattern}")):
                    return True
        return False

    if not _has_built_library():
        env = os.environ.copy()
        build_succeeded = False

        cmake_executable = _resolve_cmake_executable()
        if cmake_executable:
            sys.stderr.write(f"Using CMake at {cmake_executable}\n")
            cmake_args = [
                cmake_executable,
                "-S", str(destination),
                "-B", str(build_dir),
                "-DPCRE2_SUPPORT_JIT=ON",
                "-DPCRE2_BUILD_PCRE2_8=ON",
                "-DPCRE2_BUILD_TESTS=OFF",
                "-DPCRE2_BUILD_PCRE2GREP=OFF",
                "-DPCRE2_BUILD_PCRE2TEST=OFF",
                "-DBUILD_SHARED_LIBS=OFF",   # build a static lib
            ]
            # On Windows, force MSVC and the /MD runtime. Never let CMake pick MinGW.
            if _is_windows_platform():
                cmake_args += [
                    "-G", "Visual Studio 17 2022",
                    "-A", "x64",
                    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
                ]
            else:
                ninja = shutil.which("ninja")
                if ninja:
                    cmake_args += ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"]
                    env.setdefault("CMAKE_MAKE_PROGRAM", ninja)
                else:
                    cmake_args += ["-DCMAKE_BUILD_TYPE=Release"]

            try:
                subprocess.run(cmake_args, cwd=destination, env=env, check=True)
                build_command = [cmake_executable, "--build", str(build_dir)]
                if _is_windows_platform():
                    build_command += ["--config", "Release", "--parallel", "8"]
                else:
                    build_command += ["--parallel", "8"]
                subprocess.run(build_command, cwd=destination, env=env, check=True)
            except (FileNotFoundError, subprocess.CalledProcessError) as exc:
                raise RuntimeError(
                    "Failed to build PCRE2 from source using CMake; see the output above for details"
                ) from exc
            else:
                build_succeeded = True
        else:
            autoconf_script = destination / "configure"
            autoconf_ready = autoconf_script.exists() and not _is_windows_platform()

            if autoconf_ready:
                build_dir.mkdir(parents=True, exist_ok=True)
                sys.stderr.write(f"Using AutoConf at {autoconf_script}\n")
                try:
                    configure_command = [
                        str(autoconf_script),
                        "--enable-jit",
                        "--enable-pcre2-8",
                        "--disable-tests",
                        "--disable-pcre2grep",
                        "--disable-pcre2test",
                        "--enable-static",
                        "--disable-shared",
                    ]
                    subprocess.run(configure_command, cwd=build_dir, env=env, check=True)
                    subprocess.run(["make", "-j4"], cwd=build_dir, env=env, check=True)
                except FileNotFoundError as exc:
                    raise RuntimeError(
                        "Building PCRE2 from source via Autoconf requires the GNU build toolchain (configure/make) to be available on PATH"
                    ) from exc
                except subprocess.CalledProcessError as exc:
                    raise RuntimeError(
                        "Failed to build PCRE2 from source using Autoconf; see the output above for details"
                    ) from exc
                else:
                    build_succeeded = True

        if not build_succeeded:
            raise RuntimeError(
                "PCRE2 build tooling was not found. Install CMake or Autoconf (configure/make) to build from source."
            )

    header_source = destination / "src" / "pcre2.h.generic"
    header_target = destination / "src" / "pcre2.h"
    if header_source.exists() and not header_target.exists():
        shutil.copy2(header_source, header_target)

    include_target = _get_pcre_ext_dir() / "pcre2.h"
    if header_target.exists():
        shutil.copy2(header_target, include_target)

    include_dirs: list[str] = []
    library_dirs: list[str] = []
    library_files: list[str] = []
    seen_includes: set[str] = set()
    seen_lib_dirs: set[str] = set()
    seen_lib_files: set[str] = set()

    def _add_include(path: Path) -> None:
        path = path.resolve()
        path_str = str(path)
        if path.is_dir() and path_str not in seen_includes:
            include_dirs.append(path_str)
            seen_includes.add(path_str)

    def _add_library_file(path: Path) -> None:
        path = path.resolve()
        if not path.is_file():
            return
        path_str = str(path)
        if path_str not in seen_lib_files:
            library_files.append(path_str)
            seen_lib_files.add(path_str)
        parent = str(path.parent.resolve())
        if parent not in seen_lib_dirs:
            library_dirs.append(parent)
            seen_lib_dirs.add(parent)

    include_dir = destination / "src"
    _add_include(include_dir)

    search_roots = [
        destination,
        destination / "src",
        destination / ".libs",
        destination / "src" / ".libs",
        build_dir,
        build_dir / "lib",
        build_dir / "bin",
        build_dir / "Release",
        build_dir / "Debug",
        build_dir / "RelWithDebInfo",
        build_dir / "MinSizeRel",
    ]
    for root in search_roots:
        if not root.exists():
            continue
        for pattern in _get_library_search_patterns():
            for path in root.glob(pattern):
                _add_library_file(path)

    # On Windows, never feed GCC/MinGW archives to MSVC.
    if _is_windows_platform():
        library_files = [p for p in library_files if p.lower().endswith(".lib")]

    if not library_files:
        raise RuntimeError(
            "PCRE2 build did not produce any libpcre2-8 artifacts; check the build output for errors"
        )

    return (include_dirs, library_dirs, library_files)


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


def _compiler_supports_flags(
    flags: Iterable[str], *, code: str | None = None
) -> bool:
    normalized = tuple(str(flag) for flag in flags if flag)
    source_code = code or "int main(void) { return 0; }\n"
    cache_key = (normalized, source_code)

    cached = _COMPILER_FLAG_COMBO_CACHE.get(cache_key)
    if cached is not None:
        return cached

    compiler = _get_test_compiler()
    if compiler is None:
        _COMPILER_FLAG_COMBO_CACHE[cache_key] = False
        return False

    with tempfile.TemporaryDirectory() as tmpdir:
        source_path = Path(tmpdir) / "flag_check.c"
        source_path.write_text(source_code, encoding="utf-8")
        try:
            compiler.compile(
                [str(source_path)],
                output_dir=tmpdir,
                extra_postargs=list(normalized),
            )
        except (CCompilerError, DistutilsExecError, OSError):
            _COMPILER_FLAG_COMBO_CACHE[cache_key] = False
        else:
            _COMPILER_FLAG_COMBO_CACHE[cache_key] = True

    return _COMPILER_FLAG_COMBO_CACHE[cache_key]


def _compiler_supports_flag(flag: str) -> bool:
    cached = _COMPILER_FLAG_CACHE.get(flag)
    if cached is not None:
        return cached

    result = _compiler_supports_flags([flag])
    _COMPILER_FLAG_CACHE[flag] = result
    return result


def _augment_compile_flags(flags: list[str]) -> None:
    if _is_truthy_env("PYPCRE_DISABLE_OPT_FLAGS"):
        return

    if _is_windows_platform():
        return

    disable_native = _is_truthy_env("PYPCRE_DISABLE_NATIVE_FLAGS")
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

    env_root = os.environ.get("PYPCRE_ROOT")
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
    elif _is_solaris_platform():
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
    elif _is_solaris_platform():
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


def _extend_env_paths(target: list[str], env_var: str) -> None:
    value = os.environ.get(env_var)
    if not value:
        return
    for raw_path in value.split(os.pathsep):
        candidate = raw_path.strip()
        if candidate:
            _extend_unique(target, candidate)

def _header_exists(directory: Path) -> bool:
    return (directory / "pcre2.h").exists()


def _library_exists(directory: Path) -> bool:
    return _locate_library_file(directory) is not None


def _locate_library_file(directory: Path) -> Path | None:
    if not directory.exists():
        return None
    basename = _get_library_basename()
    for extension in _get_lib_extensions():
        candidate = directory / f"{basename}{extension}"
        if candidate.exists():
            return candidate
    for candidate in directory.glob(f"{basename}.so.*"):
        if candidate.exists():
            return candidate
    fallback = directory / f"{basename}.dll"
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


# Public helper aliases imported by setup.py
prepare_pcre2_source = _prepare_pcre2_source
extend_unique = _extend_unique
extend_env_paths = _extend_env_paths
run_pkg_config = _run_pkg_config
find_library_with_pkg_config = _find_library_with_pkg_config
find_library_with_ldconfig = _find_library_with_ldconfig
find_library_with_brew = _find_library_with_brew
discover_library_dirs = _discover_library_dirs
discover_include_dirs = _discover_include_dirs
locate_library_file = _locate_library_file
header_exists = _header_exists
library_exists = _library_exists
augment_compile_flags = _augment_compile_flags
compiler_supports_flag = _compiler_supports_flag
compiler_supports_flags = _compiler_supports_flags
has_header = _has_header
has_library = _has_library
is_truthy_env = _is_truthy_env
is_windows_platform = _is_windows_platform
is_solaris_platform = _is_solaris_platform


__all__ = [
    "configure_environment",
    "prepare_pcre2_source",
    "extend_unique",
    "extend_env_paths",
    "run_pkg_config",
    "find_library_with_pkg_config",
    "find_library_with_ldconfig",
    "find_library_with_brew",
    "discover_library_dirs",
    "discover_include_dirs",
    "locate_library_file",
    "header_exists",
    "library_exists",
    "augment_compile_flags",
    "compiler_supports_flag",
    "compiler_supports_flags",
    "has_header",
    "has_library",
    "is_truthy_env",
    "is_windows_platform",
    "is_solaris_platform",
]
