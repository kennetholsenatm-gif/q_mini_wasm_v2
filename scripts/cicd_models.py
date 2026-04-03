#!/usr/bin/env python3
"""CI/CD Agent Manager - Data models."""

import json
from datetime import datetime
from pathlib import Path
from typing import Dict, Optional, Any
from dataclasses import dataclass, asdict
from enum import Enum


class AgentStatus(Enum):
    IDLE = "idle"
    RUNNING = "running"
    STUCK = "stuck"
    FAILED = "failed"
    RECOVERING = "recovering"


class PipelineStage(Enum):
    CHECKOUT = "checkout"
    BUILD = "build"
    TEST = "test"
    ANALYSIS = "analysis"
    DEPLOY = "deploy"


@dataclass
class AgentInfo:
    name: str
    status: AgentStatus
    current_task: Optional[str]
    last_heartbeat: str
    pipeline_stage: Optional[PipelineStage]
    error_count: int
    success_count: int
    avg_runtime_seconds: float
    resources: Dict[str, Any]


@dataclass
class PipelineJob:
    job_id: str
    workflow_name: str
    status: AgentStatus
    started_at: str
    stage: PipelineStage
    agent_assigned: Optional[str]
    retry_count: int
    error_message: Optional[str]
