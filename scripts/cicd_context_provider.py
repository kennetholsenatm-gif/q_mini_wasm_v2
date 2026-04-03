#!/usr/bin/env python3
"""CI/CD RAG Context Provider - Provides pipeline context for RAG queries."""

import json
from pathlib import Path
from datetime import datetime


class CICDContextProvider:
    """Provides CI/CD pipeline context for RAG queries."""
    
    def __init__(self):
        self.state_file = Path("agents/memory/cicd_state.json")
        self.workflow_dir = Path(".github/workflows")
    
    def get_pipeline_context(self):
        """Get current pipeline context for RAG."""
        context = {
            "timestamp": datetime.utcnow().isoformat(),
            "pipelines": self._get_workflow_info(),
            "agent_state": self._get_agent_state(),
            "recommendations": self._generate_recommendations()
        }
        return context
    
    def _get_workflow_info(self):
        """Get information about GitHub Actions workflows."""
        workflows = []
        if self.workflow_dir.exists():
            for wf_file in self.workflow_dir.glob("*.yml"):
                try:
                    content = wf_file.read_text()
                    workflows.append({
                        "name": wf_file.name,
                        "triggers": self._extract_triggers(content),
                        "jobs_count": content.count("runs-on:")
                    })
                except Exception:
                    pass
        return workflows
    
    def _extract_triggers(self, content):
        """Extract workflow triggers from YAML content."""
        triggers = []
        if "on:" in content:
            lines = content.split("\n")
            in_on_section = False
            for line in lines:
                if line.strip().startswith("on:"):
                    in_on_section = True
                elif in_on_section and line and not line.startswith(" ") and not line.startswith("\t"):
                    break
                elif in_on_section:
                    trigger = line.strip().rstrip(":")
                    if trigger and not trigger.startswith("#"):
                        triggers.append(trigger)
        return triggers
    
    def _get_agent_state(self):
        """Get current agent state from persisted state."""
        if self.state_file.exists():
            try:
                state = json.loads(self.state_file.read_text())
                return {
                    "agent_count": len(state.get("agents", [])),
                    "last_updated": state.get("timestamp", "unknown")
                }
            except Exception:
                pass
        return {"agent_count": 0, "last_updated": "never"}
    
    def _generate_recommendations(self):
        """Generate recommendations based on current state."""
        recommendations = []
        agent_state = self._get_agent_state()
        
        if agent_state["agent_count"] == 0:
            recommendations.append("No CI/CD agents registered. Consider registering agents for pipeline execution.")
        
        workflows = self._get_workflow_info()
        if len(workflows) > 5:
            recommendations.append(f"Found {len(workflows)} workflows. Consider consolidating related workflows.")
        
        return recommendations


def main():
    """CLI interface for CI/CD context provider."""
    provider = CICDContextProvider()
    context = provider.get_pipeline_context()
    print(json.dumps(context, indent=2))


if __name__ == "__main__":
    main()
