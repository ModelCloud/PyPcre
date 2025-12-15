import setup_utils


def test_filter_incompatible_multiarch_skips_foreign_arch(monkeypatch):
    monkeypatch.setattr(setup_utils, "_linux_multiarch_dirs", lambda: ["x86_64-linux-gnu"])
    paths = [
        "/usr/lib/x86_64-linux-gnu/libpcre2-8.so",
        "/usr/lib/i386-linux-gnu/libpcre2-8.so.0",
        "/opt/lib/libpcre2-8.so",
    ]

    result = setup_utils.filter_incompatible_multiarch(paths)

    assert result == [
        "/usr/lib/x86_64-linux-gnu/libpcre2-8.so",
        "/opt/lib/libpcre2-8.so",
    ]


def test_filter_incompatible_multiarch_keeps_host_arch(monkeypatch):
    monkeypatch.setattr(setup_utils, "_linux_multiarch_dirs", lambda: ["aarch64-linux-gnu"])
    paths = [
        "/usr/lib/x86_64-linux-gnu/libpcre2-8.so",
        "/usr/lib/aarch64-linux-gnu/libpcre2-8.so",
        "/usr/lib/i386-linux-gnu/libpcre2-8.so.0",
    ]

    result = setup_utils.filter_incompatible_multiarch(paths)

    assert result == ["/usr/lib/aarch64-linux-gnu/libpcre2-8.so"]
