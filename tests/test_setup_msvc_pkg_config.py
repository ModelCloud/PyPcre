import importlib.util
import sys
from pathlib import Path
from unittest import mock

import pytest

import setup_utils


ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def setup_module():
    stubs = {
        "configure_environment": lambda **kwargs: None,
        "prepare_pcre2_source": lambda: ([], [], []),
        "discover_include_dirs": lambda: [],
        "discover_library_dirs": lambda: [],
        "find_library_with_pkg_config": lambda: [],
        "find_library_with_ldconfig": lambda: [],
        "find_library_with_brew": lambda: [],
        "locate_library_file": lambda directory: None,
        "has_header": lambda include_dirs: True,
        "has_library": lambda library_dirs: True,
        "ensure_python_headers": lambda include_dirs: None,
        "compiler_supports_flag": lambda flag, code=None: True,
        "compiler_supports_flags": lambda flags, code=None: True,
        "augment_compile_flags": lambda flags: None,
        "is_solaris_platform": lambda: False,
    }
    module_name = "pypcre_setup_under_test"
    spec = importlib.util.spec_from_file_location(module_name, ROOT / "setup.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)

    with mock.patch.multiple(setup_utils, **stubs), mock.patch("setuptools.setup"):
        sys.modules[module_name] = module
        spec.loader.exec_module(module)

    yield module
    sys.modules.pop(module_name, None)


def test_is_wsl_environment_is_exported():
    assert setup_utils.is_wsl_environment is setup_utils._is_wsl_environment
    assert "is_wsl_environment" in setup_utils.__all__


def test_native_windows_skips_all_pkg_config_lookups(monkeypatch, setup_module):
    run_pkg_config = mock.Mock(return_value=[])
    find_with_pkg_config = mock.Mock(return_value=["C:/mingw64/lib/libpcre2-8.dll.a"])
    monkeypatch.setattr(setup_module, "is_windows_platform", lambda: True)
    monkeypatch.setattr(setup_module, "is_wsl_environment", lambda: False)
    monkeypatch.setattr(setup_module, "run_pkg_config", run_pkg_config)
    monkeypatch.setattr(setup_module, "find_library_with_pkg_config", find_with_pkg_config)
    monkeypatch.setattr(setup_module, "compiler_supports_flags", lambda flags, code=None: True)
    monkeypatch.setattr(setup_module, "compiler_supports_flag", lambda flag, code=None: True)

    config = setup_module.collect_build_config()

    run_pkg_config.assert_not_called()
    find_with_pkg_config.assert_not_called()
    assert "pcre2-8" in config["libraries"]
    assert not any("mingw" in value.lower() for value in config["include_dirs"])
    assert not any("mingw" in value.lower() for value in config["library_dirs"])
    assert not any("mingw" in value.lower() for value in config["extra_link_args"])


def test_wsl_windows_uses_pkg_config(monkeypatch, setup_module):
    def fake_pkg_config(argument):
        if argument == "--cflags":
            return ["-I/mingw64/include", "-DPCRE2_CODE_UNIT_WIDTH=8"]
        return ["-L/mingw64/lib", "-lpcre2-8"]

    run_pkg_config = mock.Mock(side_effect=fake_pkg_config)
    find_with_pkg_config = mock.Mock(return_value=[])
    monkeypatch.setattr(setup_module, "is_windows_platform", lambda: True)
    monkeypatch.setattr(setup_module, "is_wsl_environment", lambda: True)
    monkeypatch.setattr(setup_module, "run_pkg_config", run_pkg_config)
    monkeypatch.setattr(
        setup_module,
        "find_library_with_pkg_config",
        find_with_pkg_config,
    )
    monkeypatch.setattr(setup_module, "compiler_supports_flags", lambda flags, code=None: True)
    monkeypatch.setattr(setup_module, "compiler_supports_flag", lambda flag, code=None: True)

    config = setup_module.collect_build_config()

    assert run_pkg_config.call_args_list == [
        mock.call("--cflags"),
        mock.call("--libs"),
    ]
    find_with_pkg_config.assert_called_once_with()
    assert "/mingw64/include" in config["include_dirs"]
    assert ("PCRE2_CODE_UNIT_WIDTH", "8") in config["define_macros"]
    assert "/mingw64/lib" in config["library_dirs"]
    assert "pcre2-8" in config["libraries"]
