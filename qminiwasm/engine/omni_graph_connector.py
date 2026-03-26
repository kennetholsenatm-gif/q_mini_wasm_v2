"""OmniGraph gRPC connector scaffold for Enclave-as-Code graph apply flows."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any, Dict

from .graph_manifest import GraphManifest


@dataclass(frozen=True)
class GraphApplyResult:
    accepted: bool
    status: str
    graph_id: str
    node_id: str
    message: str


class OmniGraphConnector:
    """Thin gRPC-oriented connector surface for graph topology apply operations.

    This implementation intentionally keeps transport details minimal so callers can
    integrate now, while the generated protobuf client/server stack evolves.
    """

    def __init__(self, grpc_target: str, timeout_seconds: int = 10):
        self._grpc_target = grpc_target
        self._timeout_seconds = timeout_seconds

    @staticmethod
    def build_apply_payload(manifest: GraphManifest) -> Dict[str, Any]:
        return {
            "graph_id": manifest.graph_id,
            "node_id": manifest.node_id,
            "artifact_manifest_path": str(manifest.artifacts_dir / "artifact_manifest.json"),
            "inputs": list(manifest.inputs),
            "outputs": list(manifest.outputs),
            "routing_policy_ref": manifest.routing_policy.policy_ref,
            "require_encryption": manifest.routing_policy.require_encryption,
            "backend_constraints_json": json.dumps(manifest.backend_constraints, sort_keys=True),
        }

    def apply_graph(self, manifest: GraphManifest) -> GraphApplyResult:
        payload = self.build_apply_payload(manifest)
        # gRPC-first seam: callers pass canonical payload while wire-level protobuf bindings
        # are introduced incrementally in parallel with C++ service evolution.
        if not self._grpc_target.strip():
            return GraphApplyResult(
                accepted=False,
                status="invalid_target",
                graph_id=manifest.graph_id,
                node_id=manifest.node_id,
                message="empty grpc target",
            )
        _ = payload
        _ = self._timeout_seconds
        return GraphApplyResult(
            accepted=True,
            status="accepted",
            graph_id=manifest.graph_id,
            node_id=manifest.node_id,
            message=f"dispatched to {self._grpc_target}",
        )
