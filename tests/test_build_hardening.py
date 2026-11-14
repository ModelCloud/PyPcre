# SPDX-FileCopyrightText: 2026 ModelCloud.ai
# SPDX-FileCopyrightText: 2026 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Build-time source verification and environment handling (#132)."""

from __future__ import annotations

import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

import pytest

import setup_utils

import pcre

ROOT = Path(__file__).resolve().parents[1]


def _tar_with(tmp_path: Path, add) -> Path:
    archive = tmp_path / "archive.tar"
    with tarfile.open(archive, "w") as tar:
        add(tar)
    return archive


def _add_file(tar: tarfile.TarFile, name: str, data: bytes = b"ok") -> None:
    info = tarfile.TarInfo(name)
    info.size = len(data)
    tar.addfile(info, io.BytesIO(data))


def _add_link(tar: tarfile.TarFile, name: str, target: str, kind: bytes) -> None:
    info = tarfile.TarInfo(name)
    info.type = kind
    info.linkname = target
    tar.addfile(info)


def test_safe_extract_accepts_regular_members(tmp_path):
    archive = _tar_with(tmp_path, lambda tar: _add_file(tar, "Python-3.x/Include/Python.h"))
    dest = tmp_path / "out"
    dest.mkdir()
    with tarfile.open(archive) as tar:
        setup_utils._safe_extract(tar, dest, tar.getmembers())
    assert (dest / "Python-3.x/Include/Python.h").read_bytes() == b"ok"


@pytest.mark.parametrize("kind", [tarfile.SYMTYPE, tarfile.LNKTYPE], ids=["symlink", "hardlink"])
def test_safe_extract_rejects_links(tmp_path, kind):
    def add(tar):
        _add_link(tar, "Python-3.x/Include/pyconfig.h", "/etc/passwd", kind)

    archive = _tar_with(tmp_path, add)
    dest = tmp_path / "out"
    dest.mkdir()
    with tarfile.open(archive) as tar, pytest.raises(RuntimeError, match="unsafe archive member"):
        setup_utils._safe_extract(tar, dest, tar.getmembers())
    assert not (dest / "Python-3.x").exists()


@pytest.mark.parametrize("name", ["../escape.h", "/tmp/abs.h", "Python-3.x/../../escape.h"])
def test_safe_extract_rejects_traversal(tmp_path, name):
    archive = _tar_with(tmp_path, lambda tar: _add_file(tar, name))
    dest = tmp_path / "out"
    dest.mkdir()
    with tarfile.open(archive) as tar, pytest.raises(RuntimeError, match="unsafe path"):
        setup_utils._safe_extract(tar, dest, tar.getmembers())


def _git(cwd: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=cwd, check=True, capture_output=True, text=True
    ).stdout.strip()


@pytest.fixture
def git_checkout(tmp_path):
    if shutil.which("git") is None:
        pytest.skip("git not available")
    repo = tmp_path / "pcre2"
    repo.mkdir()
    _git(repo, "init", "-q")
    (repo / "README").write_text("pcre2\n")
    _git(repo, "add", "README")
    _git(repo, "-c", "user.name=t", "-c", "user.email=t@example.invalid", "commit", "-q", "-m", "init")
    return repo, _git(repo, "rev-parse", "HEAD")


@pytest.fixture
def pinned_commit(monkeypatch):
    def pin(value):
        monkeypatch.setattr(setup_utils, "_PCRE2_COMMIT", value)
        monkeypatch.setattr(setup_utils, "_PCRE2_TAG", "pcre2-test")

    return pin


def test_verify_checkout_accepts_pinned_commit(git_checkout, pinned_commit):
    repo, head = git_checkout
    pinned_commit(head)
    setup_utils._verify_pcre2_checkout(repo)


def test_verify_checkout_rejects_moved_tag(git_checkout, pinned_commit):
    repo, _head = git_checkout
    pinned_commit("0" * 40)
    with pytest.raises(RuntimeError, match="expected pinned commit"):
        setup_utils._verify_pcre2_checkout(repo)


def test_setup_pins_the_submodule_commit():
    # setup.py's pin must match the gitlink recorded for the vendored submodule.
    text = (ROOT / "setup.py").read_text()
    pinned = text.split('PCRE2_COMMIT = "', 1)[1].split('"', 1)[0]
    if shutil.which("git") is None or not (ROOT / ".git").exists():
        pytest.skip("not running from a git checkout")
    try:
        tree = _git(ROOT, "ls-tree", "HEAD", "pcre_ext/pcre2-10.46")
    except subprocess.CalledProcessError:
        pytest.skip("submodule gitlink unavailable")
    assert tree.split()[2] == pinned


def _run_isolated(flag: str, env_overrides: dict[str, str]) -> dict[str, str]:
    """Run a probe under ``-E``/``-I``, which also ignore PYTHONPATH."""

    search_path = [str(Path(pcre.__file__).resolve().parents[1]), str(Path(pcre.pcre_ext_c.__file__).resolve().parent)]
    script = (
        "import json, sys\n"
        f"sys.path[:0] = {search_path!r}\n"
        "import pcre, pcre_ext_c as C\n"
        "print(json.dumps({'py': pcre.cache_strategy(), 'c': C.get_cache_strategy(), "
        "'allocator': C.get_allocator()}))\n"
    )
    env = os.environ.copy()
    env.update(env_overrides)
    args = [sys.executable] + ([flag] if flag else []) + ["-c", script]
    completed = subprocess.run(args, check=True, capture_output=True, text=True, env=env, cwd=ROOT)
    return json.loads(completed.stdout)


@pytest.mark.parametrize("variable", ["PYPCRE_CACHE_PATTERN_GLOBAL", "PCRE2_CACHE_PATTERN_GLOBAL"])
def test_cache_strategy_layers_agree(variable):
    honoured = _run_isolated("", {variable: "1"})
    assert honoured["py"] == honoured["c"] == "global"
    ignored = _run_isolated("-E", {variable: "1"})
    assert ignored["py"] == ignored["c"] == "thread-local"


@pytest.mark.parametrize("flag", ["-E", "-I"])
def test_allocator_override_ignored_in_isolated_modes(flag):
    assert _run_isolated(flag, {"PYPCRE_ALLOCATOR": "malloc"})["allocator"] == "pymem"
    assert _run_isolated("", {"PYPCRE_ALLOCATOR": "malloc"})["allocator"] == "malloc"
