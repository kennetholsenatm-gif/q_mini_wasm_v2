from mcp.server.fastmcp import FastMCP
import os

mcp = FastMCP("Native")

import subprocess
import os
import json
from typing import Dict, Any

@mcp.tool()
def native_compile(source: str = "cpp/", target: str = "x86_64") -> Dict[str, Any]:
    """Compile native code"""
    try:
        # Compile native code
        result = subprocess.run(['cmake', '-S', source, '-B', 'build', '-DCMAKE_BUILD_TYPE=Release'],
                              capture_output=True, text=True, check=True)
        result = subprocess.run(['cmake', '--build', 'build', '--target', 'all'],
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
def native_optimize(binary: str = "", level: str = "3") -> Dict[str, Any]:
    """Optimize native code"""
    try:
        # Optimize native binary
        result = subprocess.run(['opt', binary, '-O' + level, '-o', 'optimized'],
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
def native_profile(binary: str = "", metrics: str = "performance") -> Dict[str, Any]:
    """Profile native code"""
    try:
        # Profile native binary
        result = subprocess.run(['perf', 'record', binary],
                              capture_output=True, text=True, check=True)
        result = subprocess.run(['perf', 'report'],
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
def native_debug(binary: str = "", breakpoint: str = "") -> Dict[str, Any]:
    """Debug native code"""
    try:
        # Debug native binary
        result = subprocess.run(['gdb', binary, '--batch', '--ex', f'break {breakpoint}', '--ex', 'run'],
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
def native_deploy(binary: str = "", target: str = "local") -> Dict[str, Any]:
    """Deploy native binaries"""
    try:
        # Deploy native binary
        result = subprocess.run(['deploy', binary, target],
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
def native_monitor(binary: str = "", metrics: str = "performance") -> Dict[str, Any]:
    """Monitor native execution"""
    try:
        # Monitor native execution
        result = subprocess.run(['monitor', binary, metrics],
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

@mcp.resource("file://docs/native-documentation.md")
def get_native_documentation() -> str:
    """Native execution documentation"""
    try:
        with open("docs/native-documentation.md", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

@mcp.resource("file://docs/NATIVE_ROLLOUT_CHECKLIST.md")
def get_performance_tuning() -> str:
    """Native performance tuning"""
    try:
        with open("docs/NATIVE_ROLLOUT_CHECKLIST.md", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

if __name__ == '__main__':
    mcp.run()
