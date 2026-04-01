from mcp.server.fastmcp import FastMCP
import os

mcp = FastMCP("C++ Programming")

import subprocess
import os
import json
from typing import Dict, Any

@mcp.tool()
def cpp_compile(source: str = "cpp/", build_type: str = "Release") -> Dict[str, Any]:
    """Compile C++ code with CMake"""
    try:
        # Create build directory
        build_dir = os.path.join(source, "build")
        os.makedirs(build_dir, exist_ok=True)
        
        # Configure CMake
        result = subprocess.run(['cmake', '-S', source, '-B', build_dir, f'-DCMAKE_BUILD_TYPE={build_type}'],
                              capture_output=True, text=True, check=True)
        
        # Build
        result = subprocess.run(['cmake', '--build', build_dir],
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
def cpp_build(target: str = "all") -> Dict[str, Any]:
    """Build C++ components"""
    try:
        result = subprocess.run(['cmake', '--build', 'cpp/build', '--target', target],
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
def cpp_test(test_name: str = "") -> Dict[str, Any]:
    """Run C++ tests"""
    try:
        if test_name:
            result = subprocess.run(['ctest', '-R', test_name, '--output-on-failure'],
                                  capture_output=True, text=True, check=True)
        else:
            result = subprocess.run(['ctest', '--output-on-failure'],
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
def cpp_analyze(pattern: str = "ternary") -> Dict[str, Any]:
    """Analyze C++ code for patterns"""
    try:
        # Search for pattern in C++ files
        result = subprocess.run(['grep', '-r', pattern, 'cpp/'],
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
def cpp_refactor(file: str, changes: str) -> Dict[str, Any]:
    """Refactor C++ code"""
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
def cpp_document(format: str = "Doxygen") -> Dict[str, Any]:
    """Generate C++ documentation"""
    try:
        if format == "Doxygen":
            result = subprocess.run(['doxygen', 'Doxyfile'],
                                  capture_output=True, text=True, check=True)
        elif format == "Sphinx":
            result = subprocess.run(['sphinx-build', '-b', 'html', 'docs', 'build'],
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

@mcp.resource("file://docs/cpp-documentation.md")
def get_cpp_documentation() -> str:
    """C++ documentation and guides"""
    try:
        with open("docs/cpp-documentation.md", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

@mcp.resource("file://docs/research/")
def get_whitepapers() -> str:
    """C++ architecture whitepapers"""
    try:
        with open("docs/research/", "r", encoding="utf-8") as f:
            return f.read()
    except Exception as e:
        return f"Error reading resource: {e}"

if __name__ == '__main__':
    mcp.run()
