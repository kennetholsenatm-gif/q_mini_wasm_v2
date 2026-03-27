"""``python -m qminiwasm.cli`` — thin wrapper over :mod:`qminiwasm.engine` for training."""

from __future__ import annotations

import runpy
import sys

from qminiwasm.engine.graph_manifest import validate_graph_manifest


def _usage() -> str:
    return (
        "usage: python -m qminiwasm.cli train [--config PATH] [--log-level LEVEL]\n"
        "       python -m qminiwasm.cli graph validate <manifest.json>\n"
        "       python -m qminiwasm.cli graph apply <manifest.json>\n"
        "\n"
        "  train   Same options as ``python -m qminiwasm.engine``.\n"
        "  graph   Enclave-as-Code graph manifest flows.\n"
        "\n"
        "WASM runtime code lives under ``qminiwasm.wasm_host``.\n"
    )


def _handle_graph(argv: list[str]) -> int:
    if not argv or argv[0] in ("-h", "--help"):
        sys.stdout.write("usage: python -m qminiwasm.cli graph <validate|apply> <manifest.json>\n")
        return 0
    if len(argv) != 2:
        sys.stderr.write("graph command expects exactly 2 args: <validate|apply> <manifest.json>\n")
        return 2
    action, manifest_path = argv
    if action == "validate":
        try:
            m = validate_graph_manifest(manifest_path)
            sys.stdout.write(
                f"OK graph validate graph_id={m.graph_id} node_id={m.node_id} "
                f"artifacts_dir={m.artifacts_dir}\n"
            )
            return 0
        except Exception as exc:
            sys.stderr.write(f"GRAPH_VALIDATE_FAIL: {exc}\n")
            return 1
    if action == "apply":
        try:
            from qminiwasm.engine.graph_apply import dispatch_graph_manifest

            result = dispatch_graph_manifest(manifest_path)
            sys.stdout.write(
                "OK graph apply "
                f"accepted={str(bool(result['accepted'])).lower()} "
                f"status={result['status']} "
                f"graph_id={result['graph_id']} node_id={result['node_id']}\n"
            )
            return 0
        except Exception as exc:
            sys.stderr.write(f"GRAPH_APPLY_FAIL: {exc}\n")
            return 1
    sys.stderr.write(f"error: unknown graph action {action!r}\n")
    return 2


if __name__ == "__main__":
    argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help"):
        print(_usage(), end="" if argv else "\n")
        sys.exit(0 if argv else 1)
    if argv[0] == "train":
        sys.argv = [sys.argv[0], *argv[1:]]
        runpy.run_module("qminiwasm.engine.__main__", run_name="__main__", alter_sys=True)
    elif argv[0] == "graph":
        sys.exit(_handle_graph(argv[1:]))
    else:
        sys.stderr.write(_usage())
        sys.stderr.write(f"error: unknown command {argv[0]!r}\n")
        sys.exit(2)
