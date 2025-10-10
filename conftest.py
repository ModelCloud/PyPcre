"""Project-wide pytest configuration bridging vendored test suites."""
from _setuptools import conftest as _setuptools_conftest

_plugins = getattr(_setuptools_conftest, "pytest_plugins", ())
if isinstance(_plugins, str):
    pytest_plugins = [_plugins]
else:
    pytest_plugins = list(_plugins)
