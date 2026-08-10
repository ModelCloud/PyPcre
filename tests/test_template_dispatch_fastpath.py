from __future__ import annotations

import re
import warnings

import pytest

from pcre import pcre as pcre_module


def test_default_template_dispatch_passes_precomputed_flag(monkeypatch):
    sentinel = object()
    calls = []
    monkeypatch.setattr(pcre_module, "RE_TEMPLATE", sentinel)
    monkeypatch.setattr(
        pcre_module,
        "compile",
        lambda pattern, flags: calls.append((pattern, flags)) or "compiled",
    )

    with pytest.warns(DeprecationWarning, match="deprecated"):
        assert pcre_module.template("pattern") == "compiled"
    assert calls == [("pattern", sentinel)]


def test_dynamic_template_flags_keep_original_or_dispatch(monkeypatch):
    sentinel = object()
    calls = []

    class DynamicFlags:
        def __or__(self, other):
            calls.append((self, other))
            return sentinel

    flags = DynamicFlags()
    monkeypatch.setattr(pcre_module, "RE_TEMPLATE", re.RegexFlag.UNICODE)
    monkeypatch.setattr(
        pcre_module,
        "compile",
        lambda pattern, combined: (pattern, combined),
    )

    with pytest.warns(DeprecationWarning, match="deprecated"):
        assert pcre_module.template("pattern", flags) == ("pattern", sentinel)
    assert calls == [(flags, re.RegexFlag.UNICODE)]


def test_int_subclass_template_flags_keep_dynamic_or_dispatch(monkeypatch):
    calls = []

    class DynamicZero(int):
        def __or__(self, other):
            calls.append(other)
            return 123

    monkeypatch.setattr(pcre_module, "RE_TEMPLATE", re.RegexFlag.UNICODE)
    monkeypatch.setattr(
        pcre_module,
        "compile",
        lambda pattern, combined: (pattern, combined),
    )

    with pytest.warns(DeprecationWarning, match="deprecated"):
        assert pcre_module.template("pattern", DynamicZero()) == ("pattern", 123)
    assert calls == [re.RegexFlag.UNICODE]


def test_template_warns_on_every_call():
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always", DeprecationWarning)
        pcre_module.template("a")
        pcre_module.template("a")
    assert len(caught) == 2
    assert all(item.category is DeprecationWarning for item in caught)
