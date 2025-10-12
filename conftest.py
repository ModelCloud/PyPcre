"""Project-wide pytest configuration bridging vendored test suites.

The upstream tree optionally depends on ``_setuptools`` (shipped inside the
setuptools test suite) to pull in extra pytest plugins. When those tests are
not available – which is common in downstream packaging environments – we
should degrade gracefully rather than failing the entire run.
"""

try:
    from _setuptools import conftest as _setuptools_conftest
except ModuleNotFoundError:  # pragma: no cover - only triggered outside upstream env
    pytest_plugins: list[str] = []
else:
    _plugins = getattr(_setuptools_conftest, "pytest_plugins", ())
    if isinstance(_plugins, str):
        pytest_plugins = [_plugins]
    else:
        pytest_plugins = list(_plugins)
