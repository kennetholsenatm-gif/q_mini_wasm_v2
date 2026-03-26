"""Local enclave lifecycle event bridge for graph-control results."""

from __future__ import annotations

from dataclasses import dataclass
from typing import List

from .omni_graph_connector import GraphApplyResult

ENCLAVE_CREATE_REQUESTED = "ENCLAVE_CREATE_REQUESTED"
ENCLAVE_ATTEST_REQUESTED = "ENCLAVE_ATTEST_REQUESTED"
ENCLAVE_DESTROY_REQUESTED = "ENCLAVE_DESTROY_REQUESTED"


@dataclass(frozen=True)
class LifecycleEvent:
    event_type: str
    graph_id: str
    node_id: str
    status: str
    message: str


def lifecycle_events_from_apply_result(result: GraphApplyResult) -> List[LifecycleEvent]:
    if not result.accepted:
        return [
            LifecycleEvent(
                event_type=ENCLAVE_DESTROY_REQUESTED,
                graph_id=result.graph_id,
                node_id=result.node_id,
                status=result.status,
                message=result.message,
            )
        ]
    return [
        LifecycleEvent(
            event_type=ENCLAVE_CREATE_REQUESTED,
            graph_id=result.graph_id,
            node_id=result.node_id,
            status=result.status,
            message=result.message,
        ),
        LifecycleEvent(
            event_type=ENCLAVE_ATTEST_REQUESTED,
            graph_id=result.graph_id,
            node_id=result.node_id,
            status="pending_attestation",
            message="attestation requested for applied graph node",
        ),
    ]
