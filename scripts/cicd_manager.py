#!/usr/bin/env python3
"""CI/CD Agent Manager - Minimal version."""
import json
from datetime import datetime, timedelta
from pathlib import Path
from dataclasses import asdict
from cicd_models import AgentStatus, AgentInfo, PipelineJob

class CICDAgentManager:
    def __init__(self):
        self.agents = {}
        self.jobs = {}
        self.state_file = Path("agents/memory/cicd_state.json")
        self._load_state()
    
    def _load_state(self):
        if self.state_file.exists():
            try:
                state = json.loads(self.state_file.read_text())
                for ad in state.get("agents", []):
                    agent = AgentInfo(name=ad["name"], status=AgentStatus(ad["status"]),
                        current_task=ad.get("current_task"), last_heartbeat=ad.get("last_heartbeat", ""),
                        pipeline_stage=None, error_count=ad.get("error_count", 0),
                        success_count=ad.get("success_count", 0), avg_runtime_seconds=0, resources={})
                    self.agents[agent.name] = agent
            except Exception as e: print(f"Warning: {e}")
    
    def _save_state(self):
        self.state_file.parent.mkdir(parents=True, exist_ok=True)
        state = {"timestamp": datetime.utcnow().isoformat(), "agents": [asdict(a) for a in self.agents.values()]}
        self.state_file.write_text(json.dumps(state, indent=2))
    
    def register_agent(self, name):
        agent = AgentInfo(name=name, status=AgentStatus.IDLE, current_task=None,
            last_heartbeat=datetime.utcnow().isoformat(), pipeline_stage=None,
            error_count=0, success_count=0, avg_runtime_seconds=0, resources={})
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
                except: pass
        if stuck: self._save_state()
        return stuck
    
    def get_available_agent(self):
        for agent in self.agents.values():
            if agent.status == AgentStatus.IDLE: return agent
        return None
    
    def assign_job(self, job_id, workflow_name, stage):
        agent = self.get_available_agent()
        if not agent: return None
        job = PipelineJob(job_id=job_id, workflow_name=workflow_name, status=AgentStatus.RUNNING,
            started_at=datetime.utcnow().isoformat(), stage=stage, agent_assigned=agent.name, retry_count=0, error_message=None)
        self.jobs[job_id] = job
        agent.status = AgentStatus.RUNNING
        agent.current_task = job_id
        self._save_state()
        return agent.name
    
    def complete_job(self, job_id, success):
        if job_id not in self.jobs: return False
        job = self.jobs[job_id]
        if job.agent_assigned and job.agent_assigned in self.agents:
            agent = self.agents[job.agent_assigned]
            agent.status = AgentStatus.IDLE
            agent.current_task = None
            if success: agent.success_count += 1
            else: agent.error_count += 1
        job.status = AgentStatus.IDLE if success else AgentStatus.FAILED
        self._save_state()
        return True
    
    def recover_stuck_agents(self):
        recovered = []
        for agent in self.detect_stuck_agents():
            if agent.current_task:
                new_agent = self.get_available_agent()
                if new_agent:
                    recovered.append({"agent": agent.name, "new_agent": new_agent.name, "action": "reassigned"})
                    agent.status = AgentStatus.RECOVERING
                else:
                    self.complete_job(agent.current_task, False)
                    recovered.append({"agent": agent.name, "action": "marked_failed"})
            else:
                agent.status = AgentStatus.IDLE
                recovered.append({"agent": agent.name, "action": "reset"})
        self._save_state()
        return recovered
    
    def get_pipeline_status(self):
        counts = {s.value: 0 for s in AgentStatus}
        for a in self.agents.values(): counts[a.status.value] += 1
        active = len([j for j in self.jobs.values() if j.status == AgentStatus.RUNNING])
        failed = len([j for j in self.jobs.values() if j.status == AgentStatus.FAILED])
        return {"agents": {"total": len(self.agents), "by_status": counts},
            "jobs": {"active": active, "failed": failed, "total": len(self.jobs)},
            "health": "healthy" if failed == 0 else "degraded"}

def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["status", "register", "recover"])
    parser.add_argument("--agent-name")
    args = parser.parse_args()
    manager = CICDAgentManager()
    if args.command == "status": print(json.dumps(manager.get_pipeline_status(), indent=2))
    elif args.command == "register":
        if not args.agent_name: print("Error: --agent-name required"); return 1
        print(f"Registered: {manager.register_agent(args.agent_name).name}")
    elif args.command == "recover": print(json.dumps(manager.recover_stuck_agents(), indent=2))
    return 0

if __name__ == "__main__": exit(main())
