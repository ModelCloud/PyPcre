import platform

import pcre
import pytest


def test_ascii_vector_mode_exposed():
    mode = getattr(pcre, "_cpu_ascii_vector_mode", None)
    assert callable(mode), "_cpu_ascii_vector_mode export missing"
    value = mode()
    assert isinstance(value, int)
    if platform.machine().lower() in {"x86_64", "amd64"}:
        assert value >= 1, f"expected SIMD mode >=1 on x86_64, got {value}"
    else:
        pytest.skip("SIMD detection not supported on this architecture")
