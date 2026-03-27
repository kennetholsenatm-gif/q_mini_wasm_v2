from __future__ import annotations

from unittest.mock import patch

from qminiwasm.native_bridge import native_capabilities
from qminiwasm.runtime_modes import strict_native_enabled


def test_native_capabilities_shape_stable():
    caps = native_capabilities()
    assert "ctypes_lib_loaded" in caps
    assert "symbols" in caps
    assert "strict_native" in caps


def test_strict_native_enabled_env_toggle():
    with patch.dict("os.environ", {"QMINIWASM_NATIVE_STRICT": "1"}):
        assert strict_native_enabled() is True
    with patch.dict("os.environ", {"QMINIWASM_NATIVE_STRICT": "0"}):
        assert strict_native_enabled() is False
