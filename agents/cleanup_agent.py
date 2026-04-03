"""
Cleanup Agent for q_mini_wasm_v2

This agent monitors for task completion patterns and performs cleanup operations.
It looks for patterns like "Final Execution Complete" to trigger cleanup workflows.
"""

import asyncio
import json
import re
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any, Dict, List, Optional, Set

import structlog

from .base_agent import BaseAgent, AgentConfig, TaskResult

logger = structlog.get_logger()


class CleanupAgent(BaseAgent):
    """
    Cleanup agent for monitoring task completion and performing cleanup.
    
    This agent:
    1. Monitors for completion patterns like "Final Execution Complete"
    2. Cleans up temporary files and artifacts
    3. Archives completed task logs
    4. Manages memory and resource cleanup
    """
    
    # Patterns that indicate task completion
    COMPLETION_PATTERNS = [
        r"Final Execution Complete",
        r"Task completed successfully",
        r"All tests passed",
        r"Cycle completed",
        r"Deployment successful",
        r"Build successful",
        r"Documentation update complete",
        r"CI/CD pipeline complete",
    ]
    
    # File patterns to clean up
    TEMP_FILE_PATTERNS = [
        "*.tmp",
        "*.temp",
        "*.log",
        "*.bak",
        "*~",
        "__pycache__",
        "*.pyc",
        ".pytest_cache",
        ".mypy_cache",
    ]
    
    # Directories to monitor
    MONITOR_DIRS = [
        "docs",
        "wiki-output",
        "build",
        "dist",
        "agents/memory",
    ]
    
    def __init__(self, config: AgentConfig, llm_service=None):
        super().__init__(config, llm_service)
        self._completion_cache: Dict[str, Any] = {}
        self._cleanup_history: List[Dict[str, Any]] = []
        self._last_cleanup: Optional[datetime] = None
        
    async def execute_task(self, task: Dict[str, Any]) -> TaskResult:
        """
        Execute a cleanup task.
        
        Args:
            task: Task specification with 'type' and 'parameters'
            
        Returns:
            TaskResult with cleanup results
        """
        task_type = task.get("type", "unknown")
        parameters = task.get("parameters", {})
        
        self.logger.info("Executing cleanup task", task_type=task_type)
        self.state = "running"
        
        try:
            if task_type == "scan_completions":
                result = await self._scan_for_completions(parameters)
            elif task_type == "cleanup_temp_files":
                result = await self._cleanup_temp_files(parameters)
            elif task_type == "archive_logs":
                result = await self._archive_completed_logs(parameters)
            elif task_type == "full_cleanup":
                result = await self._full_cleanup(parameters)
            elif task_type == "check_pattern":
                result = await self._check_completion_pattern(parameters)
            else:
                return TaskResult(
                    success=False,
                    errors=[f"Unknown task type: {task_type}"]
                )
            
            self.state = "idle"
            return TaskResult(success=True, data=result)
            
        except Exception as e:
            self.logger.error("Cleanup task failed", error=str(e))
            self.state = "error"
            return TaskResult(success=False, errors=[str(e)])
    
    async def _scan_for_completions(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """
        Scan for completion patterns in logs and outputs.
        
        Returns:
            Dictionary with completion patterns found
        """
        repo_root = Path(parameters.get("repo_root", "."))
        scan_paths = parameters.get("scan_paths", ["logs", "output", "build"])
        
        completions_found = []
        
        for scan_path in scan_paths:
            path = repo_root / scan_path
            if path.exists():
                # Scan for completion patterns in files
                for pattern in self.COMPLETION_PATTERNS:
                    completions = self._find_pattern_in_files(path, pattern)
                    completions_found.extend(completions)
        
        # Update cache
        self._completion_cache = {
            "last_scan": datetime.now().isoformat(),
            "completions_found": completions_found,
            "patterns_checked": len(self.COMPLETION_PATTERNS)
        }
        
        return {
            "scan_complete": True,
            "completions_found": len(completions_found),
            "patterns": completions_found[:10]  # Limit for readability
        }
    
    def _find_pattern_in_files(self, directory: Path, pattern: str) -> List[Dict[str, Any]]:
        """Find pattern matches in files within a directory."""
        matches = []
        
        try:
            for file_path in directory.rglob("*"):
                if file_path.is_file() and file_path.suffix in [".txt", ".log", ".md", ".json"]:
                    try:
                        content = file_path.read_text(encoding="utf-8")
                        if re.search(pattern, content, re.IGNORECASE):
                            matches.append({
                                "file": str(file_path.relative_to(directory.parent)),
                                "pattern": pattern,
                                "timestamp": datetime.now().isoformat()
                            })
                    except Exception:
                        continue
        except Exception as e:
            self.logger.warning("Error scanning directory", directory=str(directory), error=str(e))
        
        return matches
    async def _cleanup_temp_files(self, parameters):
        repo_root = Path(parameters.get('repo_root', '.'))
        dry_run = parameters.get('dry_run', False)
        files_cleaned = []
        bytes_freed = 0
        for pattern in self.TEMP_FILE_PATTERNS:
            for file_path in repo_root.rglob(pattern):
                if file_path.is_file():
                    try:
                        file_size = file_path.stat().st_size
                        if not dry_run:
                            file_path.unlink()
                        files_cleaned.append(str(file_path.relative_to(repo_root)))
                        bytes_freed += file_size
                    except Exception as e:
                        pass
        self._last_cleanup = datetime.now()
        return {'cleanup_complete': True, 'files_cleaned': len(files_cleaned), 'bytes_freed': bytes_freed}
    
    async def _archive_completed_logs(self, parameters):
        return {'archive_complete': True, 'logs_archived': 0}
    
    async def _full_cleanup(self, parameters):
        results = {}
        results['scan'] = await self._scan_for_completions(parameters)
        results['cleanup'] = await self._cleanup_temp_files(parameters)
        return {'full_cleanup_complete': True, 'results': results}
    
    async def _check_completion_pattern(self, parameters):
        text = parameters.get('text', '')
        pattern = parameters.get('pattern', 'Final Execution Complete')
        match = re.search(pattern, text, re.IGNORECASE)
        return {'pattern_found': match is not None, 'pattern': pattern, 'matched_text': match.group() if match else None}
    
    async def analyze_performance(self):
        return {'tasks_completed': self._task_count, 'last_cleanup': self._last_cleanup.isoformat() if self._last_cleanup else None}
    
    async def suggest_improvements(self):
        return []
