from mcp.server.fastmcp import FastMCP
import os

mcp = FastMCP("Git Documentation")

import subprocess
import os
import json
from typing import Dict, Any

@mcp.tool()
def git_status() -> Dict[str, Any]:
    """Show current git status"""
    try:
        result = subprocess.run(['git', 'status', '--porcelain'], 
                              capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': result.stdout
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.tool()
def git_log(limit: int = 10) -> Dict[str, Any]:
    """Show commit history"""
    try:
        result = subprocess.run(['git', 'log', '--oneline', f'-{limit}'],
                              capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': result.stdout
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.tool()
def git_diff(from_ref: str = 'HEAD~1', to_ref: str = 'HEAD') -> Dict[str, Any]:
    """Show changes between commits"""
    try:
        result = subprocess.run(['git', 'diff', from_ref, to_ref],
                              capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': result.stdout
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.tool()
def git_branch(action: str = 'list', branch: str = '') -> Dict[str, Any]:
    """List and manage branches"""
    try:
        if action == 'list':
            result = subprocess.run(['git', 'branch', '-a'],
                                  capture_output=True, text=True, check=True)
            return {
                'status': 'success',
                'output': result.stdout
            }
        elif action == 'create':
            result = subprocess.run(['git', 'branch', branch],
                                  capture_output=True, text=True, check=True)
            return {
                'status': 'success',
                'output': f'Created branch {branch}'
            }
        elif action == 'delete':
            result = subprocess.run(['git', 'branch', '-d', branch],
                                  capture_output=True, text=True, check=True)
            return {
                'status': 'success',
                'output': f'Deleted branch {branch}'
            }
        else:
            return {
                'status': 'error',
                'output': f'Unknown action: {action}'
            }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.tool()
def git_commit(message: str) -> Dict[str, Any]:
    """Create commits with message templates"""
    try:
        result = subprocess.run(['git', 'add', '.'],
                              capture_output=True, text=True, check=True)
        result = subprocess.run(['git', 'commit', '-m', message],
                              capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': f'Committed with message: {message}'
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.tool()
def git_push(remote: str = 'origin', branch: str = '') -> Dict[str, Any]:
    """Push changes to remote"""
    try:
        if not branch:
            result = subprocess.run(['git', 'push', remote],
                                  capture_output=True, text=True, check=True)
        else:
            result = subprocess.run(['git', 'push', remote, branch],
                                  capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': result.stdout
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.tool()
def git_pull(remote: str = 'origin', branch: str = '') -> Dict[str, Any]:
    """Pull changes from remote"""
    try:
        if not branch:
            result = subprocess.run(['git', 'pull', remote],
                                  capture_output=True, text=True, check=True)
        else:
            result = subprocess.run(['git', 'pull', remote, branch],
                                  capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': result.stdout
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.resource("file://docs/git-documentation.md")
def get_git_documentation() -> str:
    """Git documentation and guides"""
    try:
        with open("docs/git-documentation.md", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

if __name__ == '__main__':
    mcp.run()
