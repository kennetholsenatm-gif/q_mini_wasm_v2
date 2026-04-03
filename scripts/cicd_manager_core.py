#!/usr/bin/env python3
"""CI/CD Agent Manager - Core manager class."""

import json
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, List
from dataclasses import asdict
from cicd_models import AgentStatus, PipelineStage, AgentInfo, PipelineJob


class CICDAgentManager:
    """Manages CI/CD agents and orchestrates pipeline execution."""
    
    def __init__(self, config_path="config/kanban-config.json"):
        self.config_path = config_path
        self.agents = {}
        self.jobs = {}
        self.state_file = Path("agents/memory/cicd_state.json")
        self._load_state()
    
    def _load_state(self):
        if self.state_file.exists():
            try:
                state = json.loads(self.state_file.read_text())
                for ad in state.get("agents", []):
                    agent = AgentInfo(
                        name=ad["name"], status=AgentStatus(ad["status"]),
                        current_task=ad.get("current_task"),
                        last_heartbeat=ad.get("last_heartbeat", ""),
                        pipeline_stage=PipelineStage(ad["pipeline_stage"]) if ad.get("pipeline_stage") else None,
                        error_count=ad.get("error_count", 0),
                        success_count=ad.get("success_count", 0),
                        avg_runtime_seconds=ad.get("avg_runtime_seconds", 0),
                        resources=ad.get("resources", {})
                    )
                    self.agents[agent.name] = agent
            except Exception as e:
                print(f"Warning: {e}")
    
    def _save_state(self):
        self.state_file.parent.mkdir(parents=True, exist_ok=True)
        state = {"timestamp": datetime.utcnow().isoformat(), "agents": [asdict(a) for a in self.agents.values()]}
        self.state_file.write_text(json.dumps(state, indent=2))
    
    def register_agent(self, name, resources=None):
        agent = AgentInfo(name=name, status=AgentStatus.IDLE, current_task=None,
            last_heartbeat=datetime.utcnow().isoformat(), pipeline_stage=None,
            error_count=0, success_count=0, avg_runtime_seconds=0, resources=resources or {})
        self.agents[name] = agent
        self._save_state()
        return agent
    
    def detect_stuck_agents(self, timeout_minutes=10):
        stuck = []
        cutoff = datetime.utcnow() - timedelta(minutes=timeout_minutes)
        for agent in self.agents.values():
            if agent.status in [AgentStatus.RUNNING, AgentStatus.RECOVERING]:
                try:
                    if datetime.fromisoformat(agent.last_heartbeat) < cutoff:
                        agent.status = AgentStatus.STUCK
                        stuck.append(agent)
                except Exception:
                    pass
        if stuck:
            self._save_state()
        return stuck
    
    def get_available_agent(self):
        for agent in self.agents.values():
            if agent.status == AgentStatus.IDLE:
                return agent
        return None
    
    def assign_job(self, job_id, workflow_name, stage):
        agent = self.get_available_agent()
        if not agent:
            return None
        job = PipelineJob(job_id=job_id, workflow_name=workflow_name,
            status=AgentStatus.RUNNING, started_at=datetime.utcnow().isoformat(),
            stage=stage, agent_assigned=agent.name, retry_count=0, error_message=None)
        self.jobs[job_id] = job
        agent.status = AgentStatus.RUNNING
        agent.current_task = job_id
        agent.pipeline_stage = stage
        self._save_state()
        return agent.name
