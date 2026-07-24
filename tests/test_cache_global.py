# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Coverage tests for the global pattern-cache strategy."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import textwrap
from pathlib import Path
from typing import Any, Dict


PROJECT_ROOT = Path(__file__).resolve().parents[1]


def _run_global_cache_script(source: str) -> Dict[str, Any]:
    env = os.environ.copy()
    pythonpath_entries = [str(PROJECT_ROOT)]
    existing_pythonpath = env.get("PYTHONPATH")
    if existing_pythonpath:
        pythonpath_entries.append(existing_pythonpath)
    env["PYTHONPATH"] = os.pathsep.join(pythonpath_entries)
    env["PYPCRE_CACHE_PATTERN_GLOBAL"] = "1"

    completed = subprocess.run(
        [sys.executable, "-c", source],
        check=True,
        capture_output=True,
        text=True,
        env=env,
    )
    if completed.stderr:
        raise AssertionError(f"unexpected stderr output: {completed.stderr}")
    return json.loads(completed.stdout)


def test_global_cache_strategy_is_active() -> None:
    script = textwrap.dedent(
        """
        import pcre.cache as cache_mod
        import json
        print(json.dumps({"strategy": cache_mod.cache_strategy()}))
        """
    )
    result = _run_global_cache_script(script)
    assert result["strategy"] == "global"


def test_global_cache_limit_and_clear() -> None:
    script = textwrap.dedent(
        """
        import pcre.cache as cache_mod
        import json

        def wrapper(raw):
            return raw

        cache_mod.clear_cache()
        cache_mod.set_cache_limit(2)
        cache_mod.cached_compile("a", 0, wrapper, jit=False)
        cache_mod.cached_compile("b", 0, wrapper, jit=False)
        cache_mod.cached_compile("c", 0, wrapper, jit=False)

        before = len(cache_mod._GLOBAL_STATE.pattern_cache)
        cache_mod.clear_cache()
        after = len(cache_mod._GLOBAL_STATE.pattern_cache)
        limit = cache_mod.get_cache_limit()
        print(json.dumps({"before": before, "after": after, "limit": limit}))
        """
    )
    result = _run_global_cache_script(script)
    assert result["before"] == 2
    assert result["after"] == 0
    assert result["limit"] == 2


def test_global_cache_limit_zero_disables_caching() -> None:
    script = textwrap.dedent(
        """
        import pcre.cache as cache_mod
        import json

        calls = []
        def wrapper(raw):
            calls.append(raw)
            return raw

        def fake_compile(pattern, *, flags=0, jit=False):
            calls.append(("compile", pattern))
            return pattern

        cache_mod._pcre2.compile = fake_compile
        cache_mod.clear_cache()
        cache_mod.set_cache_limit(0)

        cache_mod.cached_compile("x", 0, wrapper, jit=False)
        cache_mod.cached_compile("x", 0, wrapper, jit=False)

        print(json.dumps({"calls": calls}))
        """
    )
    result = _run_global_cache_script(script)
    assert result["calls"].count(["compile", "x"]) == 2


def test_global_cache_shrinks_to_new_limit() -> None:
    script = textwrap.dedent(
        """
        import pcre.cache as cache_mod
        import json

        def wrapper(raw):
            return raw

        def fake_compile(pattern, *, flags=0, jit=False):
            return pattern

        cache_mod._pcre2.compile = fake_compile
        cache_mod.clear_cache()
        cache_mod.set_cache_limit(10)
        for i in range(5):
            cache_mod.cached_compile(str(i), 0, wrapper, jit=False)

        cache_mod.set_cache_limit(2)
        size = len(cache_mod._GLOBAL_STATE.pattern_cache)
        print(json.dumps({"size": size}))
        """
    )
    result = _run_global_cache_script(script)
    assert result["size"] == 2
