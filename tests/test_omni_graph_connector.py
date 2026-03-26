from __future__ import annotations

from pathlib import Path

from qminiwasm.engine.graph_manifest import (
    GraphControlPlane,
    GraphManifest,
    GraphRoutingPolicy,
)
from qminiwasm.engine.lifecycle_bridge import (
    ENCLAVE_ATTEST_REQUESTED,
    ENCLAVE_CREATE_REQUESTED,
    lifecycle_events_from_apply_result,
)
from qminiwasm.engine.omni_graph_connector import OmniGraphConnector


def _sample_manifest(tmp_path: Path) -> GraphManifest:
    return GraphManifest(
        version="1",
        graph_id="g1",
        node_id="n1",
        artifacts_dir=tmp_path,
        inputs=["a"],
        outputs=["b"],
        backend_constraints={"accelerator": "cpu"},
        routing_policy=GraphRoutingPolicy(policy_ref="policy/v1", require_encryption=True),
        control_plane=GraphControlPlane(grpc_target="127.0.0.1:50051", timeout_seconds=3),
    )


def test_connector_build_payload_contains_graph_fields(tmp_path: Path):
    manifest = _sample_manifest(tmp_path)
    payload = OmniGraphConnector.build_apply_payload(manifest)
    assert payload["graph_id"] == "g1"
    assert payload["node_id"] == "n1"
    assert payload["routing_policy_ref"] == "policy/v1"


def test_connector_apply_graph_returns_accepted_for_valid_target(tmp_path: Path):
    manifest = _sample_manifest(tmp_path)
    connector = OmniGraphConnector(grpc_target=manifest.control_plane.grpc_target)
    result = connector.apply_graph(manifest)
    assert result.accepted is True
    assert result.status == "accepted"


def test_lifecycle_bridge_emits_create_and_attest(tmp_path: Path):
    manifest = _sample_manifest(tmp_path)
    connector = OmniGraphConnector(grpc_target=manifest.control_plane.grpc_target)
    result = connector.apply_graph(manifest)
    events = lifecycle_events_from_apply_result(result)
    event_types = {ev.event_type for ev in events}
    assert ENCLAVE_CREATE_REQUESTED in event_types
    assert ENCLAVE_ATTEST_REQUESTED in event_types
