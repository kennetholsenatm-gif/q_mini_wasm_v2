"""Tests for qminiwasm.engine.secret_sanitize."""

from __future__ import annotations

import os

from qminiwasm.engine.secret_sanitize import sanitize_api_key_like, sanitize_secret_environ


def test_sanitize_api_key_like_strips_quotes_and_braces() -> None:
    assert sanitize_api_key_like('"hf_abc"') == "hf_abc"
    assert sanitize_api_key_like("{hf_xyz}") == "hf_xyz"
    assert sanitize_api_key_like('"{still}"') == "still"
    assert sanitize_api_key_like("  ") is None


def test_sanitize_secret_environ_updates_os_environ() -> None:
    k = "QMW_TEST_DUMMY_TOKEN"
    os.environ[k] = '{"wrapped"}'
    try:
        sanitize_secret_environ()
        assert os.environ[k] == "wrapped"
    finally:
        del os.environ[k]
