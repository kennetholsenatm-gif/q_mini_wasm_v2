"""Graph apply orchestration helpers shared by CLI and server hooks."""

from __future__ import annotations

from typing import Any

from .graph_manifest import validate_graph_manifest
from .lifecycle_bridge import lifecycle_events_from_apply_result
from .omni_graph_connector import OmniGraphConnector


def dispatch_graph_manifest(manifest_path: str) -> dict[str, Any]:
    manifest = validate_graph_manifest(manifest_path)
    connector = OmniGraphConnector(
        grpc_target=manifest.control_plane.grpc_target,
        timeout_seconds=manifest.control_plane.timeout_seconds,
    )
    result = connector.apply_graph(manifest)
    events = lifecycle_events_from_apply_result(result)
    return {
        "accepted": result.accepted,
        "status": result.status,
        "graph_id": result.graph_id,
        "node_id": result.node_id,
        "message": result.message,
        "lifecycle_events": [
            {
                "event_type": ev.event_type,
                "graph_id": ev.graph_id,
                "node_id": ev.node_id,
                "status": ev.status,
                "message": ev.message,
            }
            for ev in events
        ],
    }
