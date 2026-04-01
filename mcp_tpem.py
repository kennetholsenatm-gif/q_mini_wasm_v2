from mcp.server.fastmcp import FastMCP
import os

mcp = FastMCP("TPEM")

import subprocess
import os
import json
import sys
from typing import Dict, Any

@mcp.tool()
def tpem_generate(model: str = "", format: str = "wasm") -> Dict[str, Any]:
    """Generate TPEM artifacts"""
    try:
        # Generate TPEM artifacts
        result = subprocess.run(['tpem', 'generate', model, format],
                              capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': result.stdout
        }
    except FileNotFoundError:
        return {
            'status': 'error',
            'output': "Command 'tpem' not found. Ensure it is installed and in PATH."
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.tool()
def tpem_validate(artifact: str = "", schema: str = "default") -> Dict[str, Any]:
    """Validate TPEM artifacts"""
    try:
        result = subprocess.run(['tpem', 'validate', artifact, schema],
                              capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': result.stdout
        }
    except FileNotFoundError:
        return {
            'status': 'error',
            'output': "Command 'tpem' not found."
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.tool()
def tpem_process(artifact: str = "", operation: str = "optimize") -> Dict[str, Any]:
    """Process TPEM artifacts"""
    try:
        result = subprocess.run(['tpem', 'process', artifact, operation],
                              capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': result.stdout
        }
    except FileNotFoundError:
        return {
            'status': 'error',
            'output': "Command 'tpem' not found."
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.tool()
def tpem_deploy(artifact: str = "", target: str = "edge") -> Dict[str, Any]:
    """Deploy TPEM artifacts"""
    try:
        result = subprocess.run(['tpem', 'deploy', artifact, target],
                              capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': result.stdout
        }
    except FileNotFoundError:
        return {
            'status': 'error',
            'output': "Command 'tpem' not found."
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.tool()
def tpem_monitor(artifact: str = "", metrics: str = "performance") -> Dict[str, Any]:
    """Monitor TPEM artifacts"""
    try:
        result = subprocess.run(['tpem', 'monitor', artifact, metrics],
                              capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': result.stdout
        }
    except FileNotFoundError:
        return {
            'status': 'error',
            'output': "Command 'tpem' not found."
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.tool()
def tpem_report(artifact: str = "", type: str = "validation") -> Dict[str, Any]:
    """Generate TPEM reports"""
    try:
        result = subprocess.run(['tpem', 'report', artifact, type],
                              capture_output=True, text=True, check=True)
        return {
            'status': 'success',
            'output': result.stdout
        }
    except FileNotFoundError:
        return {
            'status': 'error',
            'output': "Command 'tpem' not found."
        }
    except subprocess.CalledProcessError as e:
        return {
            'status': 'error',
            'output': e.stderr
        }

@mcp.resource("file://docs/tpem-documentation.md")
def get_tpem_documentation() -> str:
    """TPEM documentation and guides"""
    try:
        with open("docs/tpem-documentation.md", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

@mcp.resource("file://docs/TPEM_ARTIFACT_FORMAT.md")
def get_artifact_format() -> str:
    """TPEM artifact format documentation"""
    try:
        with open("docs/TPEM_ARTIFACT_FORMAT.md", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

if __name__ == '__main__':
    mcp.run()
