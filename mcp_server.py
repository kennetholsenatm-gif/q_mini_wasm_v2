from mcp.server.fastmcp import FastMCP
import os

mcp = FastMCP("mcp_server")

import os
import subprocess
import toml
from pathlib import Path
from typing import Dict, Any, List

@mcp.tool()
def analyze_python_code(path: str) -> Dict[str, Any]:
    """Analyze Python code structure and dependencies"""
    full_path = Path(path)
    if not full_path.exists():
        return {"error": f"Path {path} does not exist"}

    if full_path.is_file() and full_path.suffix == '.py':
        # Analyze single Python file
        with open(full_path, 'r', encoding='utf-8') as f:
            content = f.read()
        return {
            "file": str(full_path),
            "lines": len(content.splitlines()),
            "imports": [line.strip() for line in content.splitlines() if line.strip().startswith('import') or line.strip().startswith('from')]
        }
    elif full_path.is_dir():
        # Analyze directory of Python files
        py_files = list(full_path.rglob('*.py'))
        return {
            "directory": str(full_path),
            "python_files": len(py_files),
            "files": [str(f) for f in py_files]
        }
    else:
        return {"error": f"Path {path} is not a valid Python file or directory"}

@mcp.tool()
def build_cpp_components(target: str = None) -> Dict[str, Any]:
    """Build C++ components using CMake"""
    cpp_dir = Path("cpp")
    if not cpp_dir.exists():
        return {"error": "C++ directory not found"}

    try:
        # Run CMake configuration
        cmake_result = subprocess.run(
            ["cmake", "-S", str(cpp_dir), "-B", str(cpp_dir / "build")],
            capture_output=True,
            text=True,
            cwd=str(cpp_dir)
        )

        if cmake_result.returncode != 0:
            return {
                "success": False,
                "cmake_output": cmake_result.stderr,
                "error": "CMake configuration failed"
            }

        # Build the target
        build_cmd = ["cmake", "--build", str(cpp_dir / "build")]
        if target:
            build_cmd.extend(["--target", target])

        build_result = subprocess.run(
            build_cmd,
            capture_output=True,
            text=True,
            cwd=str(cpp_dir)
        )

        return {
            "success": build_result.returncode == 0,
            "output": build_result.stdout,
            "errors": build_result.stderr,
            "target": target
        }
    except Exception as e:
        return {"error": str(e)}

@mcp.tool()
def read_config(config: str) -> Dict[str, Any]:
    """Read configuration from TOML files"""
    config_path = Path("configs") / f"{config}.toml"
    if not config_path.exists():
        return {"error": f"Configuration {config} not found"}

    try:
        with open(config_path, 'r', encoding='utf-8') as f:
            config_data = toml.load(f)
        return {
            "config": config,
            "path": str(config_path),
            "data": config_data
        }
    except Exception as e:
        return {"error": str(e)}

@mcp.tool()
def execute_script(script: str, args: List[str] = None) -> Dict[str, Any]:
    """Execute a project script"""
    script_path = Path("scripts") / f"{script}.py"
    if not script_path.exists():
        return {"error": f"Script {script} not found"}

    try:
        cmd = ["python", str(script_path)]
        if args:
            cmd.extend(args)

        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            cwd="c:\\GitHub\\LLM_Pract\\qminiwasm-core"
        )

        return {
            "success": result.returncode == 0,
            "script": script,
            "output": result.stdout,
            "errors": result.stderr
        }
    except Exception as e:
        return {"error": str(e)}

@mcp.tool()
def search_docs(query: str) -> Dict[str, Any]:
    """Search through project documentation"""
    docs_dir = Path("docs")
    if not docs_dir.exists():
        return {"error": "Documentation directory not found"}

    results = []
    for doc_file in docs_dir.rglob("*.md"):
        with open(doc_file, 'r', encoding='utf-8') as f:
            content = f.read()
            if query.lower() in content.lower():
                results.append({
                    "file": str(doc_file.relative_to(docs_dir)),
                    "path": str(doc_file),
                    "matches": content.lower().count(query.lower())
                })

    return {
        "query": query,
        "results": results,
        "total_matches": len(results)
    }

@mcp.resource("file://qminiwasm")
def get_qminiwasm_core_python() -> str:
    """Python codebase for qminiwasm-core"""
    try:
        with open("qminiwasm", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

@mcp.resource("file://cpp")
def get_qminiwasm_core_cpp() -> str:
    """C++ components for qminiwasm-core"""
    try:
        with open("cpp", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

@mcp.resource("file://configs")
def get_qminiwasm_core_configs() -> str:
    """Configuration files for qminiwasm-core"""
    try:
        with open("configs", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

@mcp.resource("file://scripts")
def get_qminiwasm_core_scripts() -> str:
    """Scripts and utilities for qminiwasm-core"""
    try:
        with open("scripts", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

@mcp.resource("file://docs")
def get_qminiwasm_core_docs() -> str:
    """Documentation for qminiwasm-core"""
    try:
        with open("docs", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

if __name__ == '__main__':
    mcp.run()
