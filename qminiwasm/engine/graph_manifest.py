"""Graph sidecar manifest loading and validation for Enclave-as-Code flows."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List

from qminiwasm.wasm_host.artifact_contract import verify_artifact_contract


@dataclass(frozen=True)
class GraphControlPlane:
    grpc_target: str
    timeout_seconds: int = 10


@dataclass(frozen=True)
class GraphRoutingPolicy:
    policy_ref: str
    require_encryption: bool = True


@dataclass(frozen=True)
class GraphManifest:
    version: str
    graph_id: str
    node_id: str
    artifacts_dir: Path
    inputs: List[str]
    outputs: List[str]
    backend_constraints: Dict[str, Any]
    routing_policy: GraphRoutingPolicy
    control_plane: GraphControlPlane


def _as_non_empty_string(data: dict[str, Any], key: str, errors: list[str]) -> str:
    raw = data.get(key)
    if not isinstance(raw, str) or not raw.strip():
        errors.append(f"{key} must be a non-empty string")
        return ""
    return raw.strip()


def _as_string_list(data: dict[str, Any], key: str, errors: list[str]) -> list[str]:
    raw = data.get(key)
    if not isinstance(raw, list):
        errors.append(f"{key} must be a list of strings")
        return []
    out: list[str] = []
    for idx, item in enumerate(raw):
        if not isinstance(item, str) or not item.strip():
            errors.append(f"{key}[{idx}] must be a non-empty string")
            continue
        out.append(item.strip())
    return out


def _validate_backend_constraints(raw: Any, errors: list[str]) -> dict[str, Any]:
    if not isinstance(raw, dict):
        errors.append("backend_constraints must be an object")
        return {}
    out = dict(raw)
    bool_fields = {"sycl_required", "ternary_required"}
    string_fields = {"accelerator", "quantum_backend"}
    allowed = bool_fields | string_fields
    for key in out.keys():
        if key not in allowed:
            errors.append(f"backend_constraints.{key} is not supported")
    for key in bool_fields:
        if key in out and not isinstance(out[key], bool):
            errors.append(f"backend_constraints.{key} must be boolean")
    for key in string_fields:
        if key in out and (not isinstance(out[key], str) or not out[key].strip()):
            errors.append(f"backend_constraints.{key} must be a non-empty string when set")
    return out


def _validate_routing_policy(raw: Any, errors: list[str]) -> GraphRoutingPolicy:
    if not isinstance(raw, dict):
        errors.append("routing_policy must be an object")
        return GraphRoutingPolicy(policy_ref="")
    policy_ref = raw.get("policy_ref")
    if not isinstance(policy_ref, str) or not policy_ref.strip():
        errors.append("routing_policy.policy_ref must be a non-empty string")
        policy_ref = ""
    require_encryption = raw.get("require_encryption", True)
    if not isinstance(require_encryption, bool):
        errors.append("routing_policy.require_encryption must be boolean when set")
        require_encryption = True
    return GraphRoutingPolicy(
        policy_ref=str(policy_ref).strip(), require_encryption=bool(require_encryption)
    )


def _validate_control_plane(raw: Any, errors: list[str]) -> GraphControlPlane:
    if not isinstance(raw, dict):
        errors.append("control_plane must be an object")
        return GraphControlPlane(grpc_target="")
    grpc_target = raw.get("grpc_target")
    if not isinstance(grpc_target, str) or not grpc_target.strip():
        errors.append("control_plane.grpc_target must be a non-empty string")
        grpc_target = ""
    timeout_seconds = raw.get("timeout_seconds", 10)
    if not isinstance(timeout_seconds, int) or timeout_seconds <= 0:
        errors.append("control_plane.timeout_seconds must be a positive integer when set")
        timeout_seconds = 10
    return GraphControlPlane(grpc_target=str(grpc_target).strip(), timeout_seconds=timeout_seconds)


def _required_keys() -> set[str]:
    return {
        "version",
        "graph_id",
        "node_id",
        "artifacts_dir",
        "inputs",
        "outputs",
        "backend_constraints",
        "routing_policy",
        "control_plane",
    }


def _validate_shape(data: Any) -> list[str]:
    if not isinstance(data, dict):
        return ["manifest root must be an object"]
    errors: list[str] = []
    req = _required_keys()
    for key in req:
        if key not in data:
            errors.append(f"missing required field: {key}")
    for key in data.keys():
        if key not in req:
            errors.append(f"unknown top-level field: {key}")
    return errors


def load_graph_manifest(path: str | Path) -> GraphManifest:
    p = Path(path)
    try:
        data = json.loads(p.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON manifest: {exc}") from exc

    errors = _validate_shape(data)
    if errors:
        raise ValueError("; ".join(errors))

    assert isinstance(data, dict)
    version = _as_non_empty_string(data, "version", errors)
    graph_id = _as_non_empty_string(data, "graph_id", errors)
    node_id = _as_non_empty_string(data, "node_id", errors)
    artifacts_dir_raw = _as_non_empty_string(data, "artifacts_dir", errors)
    inputs = _as_string_list(data, "inputs", errors)
    outputs = _as_string_list(data, "outputs", errors)
    backend_constraints = _validate_backend_constraints(data.get("backend_constraints"), errors)
    routing_policy = _validate_routing_policy(data.get("routing_policy"), errors)
    control_plane = _validate_control_plane(data.get("control_plane"), errors)

    if errors:
        raise ValueError("; ".join(errors))

    artifacts_dir = Path(artifacts_dir_raw)
    if not artifacts_dir.is_absolute():
        artifacts_dir = (p.parent / artifacts_dir).resolve()
    if not artifacts_dir.is_dir():
        raise ValueError(f"artifacts_dir is not a directory: {artifacts_dir}")

    return GraphManifest(
        version=version,
        graph_id=graph_id,
        node_id=node_id,
        artifacts_dir=artifacts_dir,
        inputs=inputs,
        outputs=outputs,
        backend_constraints=backend_constraints,
        routing_policy=routing_policy,
        control_plane=control_plane,
    )


def validate_graph_manifest(path: str | Path) -> GraphManifest:
    manifest = load_graph_manifest(path)
    verify_artifact_contract(manifest.artifacts_dir)
    return manifest


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate qminiwasm graph sidecar manifest.")
    ap.add_argument("command", choices=["validate"])
    ap.add_argument("path", type=Path, help="Path to graph manifest JSON file")
    args = ap.parse_args()
    if args.command == "validate":
        try:
            m = validate_graph_manifest(args.path)
            print(
                "OK graph-manifest "
                f"graph_id={m.graph_id} node_id={m.node_id} artifacts_dir={m.artifacts_dir}"
            )
            return 0
        except Exception as exc:
            print(f"GRAPH_MANIFEST_FAIL: {exc}", file=sys.stderr)
            return 1
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
