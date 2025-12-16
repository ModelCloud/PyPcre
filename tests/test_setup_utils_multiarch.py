import struct
import sys
from pathlib import Path

import setup_utils


def _write_pe_binary(path: Path, optional_magic: int) -> None:
    e_lfanew = 0x80
    data = bytearray(b"MZ")
    if len(data) < 0x3C:
        data.extend(b"\x00" * (0x3C - len(data)))
    data.extend(struct.pack("<I", e_lfanew))
    if len(data) < e_lfanew:
        data.extend(b"\x00" * (e_lfanew - len(data)))
    data.extend(b"PE\x00\x00")
    data.extend(b"\x00" * 20)
    data.extend(struct.pack("<H", optional_magic))
    path.write_bytes(data)


def test_filter_incompatible_multiarch_filters_elf_32bit(monkeypatch, tmp_path):
    monkeypatch.setattr(setup_utils, "_linux_multiarch_dirs", lambda: ["x86_64-linux-gnu"])
    monkeypatch.setattr(setup_utils, "_host_pointer_width", lambda: 64)
    lib64 = tmp_path / "libpcre2-8.so"
    lib64.write_bytes(b"\x7fELF\x02" + b"\x00" * 16)
    lib32 = tmp_path / "libpcre2-8-compat.so"
    lib32.write_bytes(b"\x7fELF\x01" + b"\x00" * 16)

    result = setup_utils.filter_incompatible_multiarch([str(lib64), str(lib32)])

    assert result == [str(lib64)]


def test_filter_incompatible_multiarch_filters_macho_32bit(monkeypatch, tmp_path):
    monkeypatch.setattr(sys, "platform", "darwin")
    monkeypatch.setattr(setup_utils, "_linux_multiarch_dirs", lambda: [])
    monkeypatch.setattr(setup_utils, "_host_pointer_width", lambda: 64)
    macho64 = tmp_path / "libpcre2-8.dylib"
    macho32 = tmp_path / "libpcre2-8-compat.dylib"
    macho64.write_bytes(struct.pack(">I", 0xCFFAEDFE))
    macho32.write_bytes(struct.pack(">I", 0xCEFAEDFE))

    result = setup_utils.filter_incompatible_multiarch([str(macho64), str(macho32)])

    assert result == [str(macho64)]


def test_filter_incompatible_multiarch_filters_pe32(monkeypatch, tmp_path):
    monkeypatch.setattr(sys, "platform", "win32")
    monkeypatch.setattr(setup_utils, "_linux_multiarch_dirs", lambda: [])
    monkeypatch.setattr(setup_utils, "_host_pointer_width", lambda: 64)
    pe64 = tmp_path / "pcre2-8.dll"
    pe32 = tmp_path / "pcre2-8-compat.dll"
    _write_pe_binary(pe64, 0x20B)
    _write_pe_binary(pe32, 0x10B)

    result = setup_utils.filter_incompatible_multiarch([str(pe64), str(pe32)])

    assert result == [str(pe64)]
