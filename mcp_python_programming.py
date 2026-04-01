from mcp.server.fastmcp import FastMCP
import os

mcp = FastMCP("Python Programming")

import subprocess
import os
import json
from typing import Dict, Any

@mcp.tool()
def python_run(script: str = "qminiwasm/cli/main.py", args: str = "") -> Dict[str, Any]:
    """Run Python scripts"""
    try:
        command = ['python', script]
        if args:
            command.extend(args.split())
        
        result = subprocess.run(command, capture_output=True, text=True, check=True)
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
def python_test(test_name: str = "") -> Dict[str, Any]:
    """Run Python tests"""
    try:
        if test_name:
            result = subprocess.run(['pytest', '-v', test_name],
                                  capture_output=True, text=True, check=True)
        else:
            result = subprocess.run(['pytest', '-v'],
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
def python_analyze(pattern: str = "runtime") -> Dict[str, Any]:
    """Analyze Python code for patterns"""
    try:
        # Search for pattern in Python files
        result = subprocess.run(['grep', '-r', pattern, 'qminiwasm/'],
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
def python_refactor(file: str, changes: str) -> Dict[str, Any]:
    """Refactor Python code"""
    try:
        # Apply changes to file
        with open(file, 'r') as f:
            content = f.read()
        
        # Apply changes (simple replace for now)
        new_content = content.replace(changes.split('->')[0].strip(), 
                                     changes.split('->')[1].strip())
        
        with open(file, 'w') as f:
            f.write(new_content)
        
        return {
            'status': 'success',
            'output': f'Refactored {file}'
        }
    except Exception as e:
        return {
            'status': 'error',
            'output': str(e)
        }

@mcp.tool()
def python_document(format: str = "Sphinx") -> Dict[str, Any]:
    """Generate Python documentation"""
    try:
        if format == "Sphinx":
            result = subprocess.run(['sphinx-build', '-b', 'html', 'docs', 'build'],
                                  capture_output=True, text=True, check=True)
        elif format == "Markdown":
            result = subprocess.run(['pydocmd', 'generate'],
                                  capture_output=True, text=True, check=True)
        else:
            return {
                'status': 'error',
                'output': f'Unknown format: {format}'
            }
        
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
def python_profile(script: str = "qminiwasm/cli/main.py") -> Dict[str, Any]:
    """Profile Python code performance"""
    try:
        result = subprocess.run(['python', '-m', 'cProfile', '-s', 'cumtime', script],
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

@mcp.resource("file://docs/python-documentation.md")
def get_python_documentation() -> str:
    """Python documentation and guides"""
    try:
        with open("docs/python-documentation.md", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

@mcp.resource("file://docs/runtime-modes.md")
def get_runtime_modes() -> str:
    """Runtime mode documentation"""
    try:
        with open("docs/runtime-modes.md", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

if __name__ == '__main__':
    mcp.run()
