import requests
import json
from typing import Dict, Any, Optional, List
from datetime import datetime, timedelta

class ClineN8NWrapper:
    def __init__(self, base_url: str = "http://0.0.0.0:5678", api_key: Optional[str] = None):
        self.base_url = base_url
        self.api_key = api_key
        self.session = requests.Session()
        self.session.headers.update({
            'Content-Type': 'application/json',
            'Accept': 'application/json',
            'X-CSRF-Token': 'n8n-csrf-secret',
            'Origin': '*'
        })
        if api_key:
            self.session.headers.update({'Authorization': f'Bearer {api_key}'})
        
        # Rate limiting state
        self.request_count = 0
        self.window_start = datetime.now()
        self.rate_limit_window = 900  # 15 minutes in seconds
        self.max_requests = 100

    def _check_rate_limit(self) -> bool:
        """Check if we're within the 100 requests per 15 minutes limit."""
        now = datetime.now()
        if (now - self.window_start).total_seconds() > self.rate_limit_window:
            self.request_count = 0
            self.window_start = now
        
        if self.request_count >= self.max_requests:
            return False
        
        self.request_count += 1
        return True

    def _wait_for_rate_limit(self):
        """Wait if we've hit the rate limit."""
        if not self._check_rate_limit():
            time_to_wait = self.rate_limit_window - (datetime.now() - self.window_start).total_seconds()
            if time_to_wait > 0:
                import time
                time.sleep(time_to_wait + 1)  # Add 1 second buffer
            self.request_count = 0
            self.window_start = datetime.now()

    def create_workflow(self, workflow_data: Dict[str, Any]) -> Dict[str, Any]:
        """Create a new workflow in n8n."""
        self._wait_for_rate_limit()
        url = f"{self.base_url}/workflows"
        response = self.session.post(url, json=workflow_data)
        response.raise_for_status()
        return response.json()

    def get_workflow(self, workflow_id: str) -> Dict[str, Any]:
        """Get a specific workflow by ID."""
        self._wait_for_rate_limit()
        url = f"{self.base_url}/workflows/{workflow_id}"
        response = self.session.get(url)
        response.raise_for_status()
        return response.json()

    def execute_workflow(self, workflow_id: str, input_data: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Execute a workflow with optional input data."""
        self._wait_for_rate_limit()
        url = f"{self.base_url}/workflows/{workflow_id}/execute"
        payload = {"inputData": input_data} if input_data else {}
        response = self.session.post(url, json=payload)
        response.raise_for_status()
        return response.json()

    def get_workflow_executions(self, workflow_id: str, limit: int = 10) -> List[Dict[str, Any]]:
        """Get recent executions for a workflow."""
        self._wait_for_rate_limit()
        url = f"{self.base_url}/workflows/{workflow_id}/executions"
        params = {"limit": limit}
        response = self.session.get(url, params=params)
        response.raise_for_status()
        return response.json()

    def get_execution_status(self, execution_id: str) -> Dict[str, Any]:
        """Get the status of a specific execution."""
        self._wait_for_rate_limit()
        url = f"{self.base_url}/executions/{execution_id}"
        response = self.session.get(url)
        response.raise_for_status()
        return response.json()

    def get_all_workflows(self) -> List[Dict[str, Any]]:
        """Get all workflows."""
        self._wait_for_rate_limit()
        url = f"{self.base_url}/workflows"
        response = self.session.get(url)
        response.raise_for_status()
        return response.json()

    def delete_workflow(self, workflow_id: str) -> None:
        """Delete a workflow."""
        self._wait_for_rate_limit()
        url = f"{self.base_url}/workflows/{workflow_id}"
        response = self.session.delete(url)
        response.raise_for_status()

    def update_workflow(self, workflow_id: str, workflow_data: Dict[str, Any]) -> Dict[str, Any]:
        """Update an existing workflow."""
        self._wait_for_rate_limit()
        url = f"{self.base_url}/workflows/{workflow_id}"
        response = self.session.put(url, json=workflow_data)
        response.raise_for_status()
        return response.json()