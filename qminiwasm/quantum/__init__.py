"""Shim: public imports live in :mod:`qminiwasm.fabric`."""

from qminiwasm.fabric import *  # noqa: F403, F401

__all__ = ["QAHRRouter", "HybridQuantumMoE", "get_backend", "intel_backend"]  # noqa: F405
