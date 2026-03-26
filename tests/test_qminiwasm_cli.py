"""Smoke tests for ``python -m qminiwasm.cli`` (delegates training to ``qminiwasm.engine``)."""

from __future__ import annotations

import subprocess
import sys


def test_cli_help_exits_zero():
    r = subprocess.run(
        [sys.executable, "-m", "qminiwasm.cli", "--help"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert r.returncode == 0
    assert "train" in r.stdout


def test_cli_no_subcommand_exits_nonzero():
    r = subprocess.run(
        [sys.executable, "-m", "qminiwasm.cli"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert r.returncode != 0
    assert "train" in r.stdout or "train" in r.stderr
