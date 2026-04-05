#!/usr/bin/env python3
"""
Architecture Enforcement Script for q_mini_wasm_v2

This script scans the codebase to ensure that the strict architectural constraints 
defined in docs/research/Quantum-Classical System Architectural Review.md are obeyed.
It acts as a physical barrier against the re-introduction of:
- PPO / Reinforcement Learning
- GRPC and non-MCP HTTP Servers
- Dense Symplectic Matrix Tableaus
"""

import os
import re
import sys
from pathlib import Path

# ANSI escape codes for coloring
RED = '\033[91m'
GREEN = '\033[92m'
YELLOW = '\033[93m'
RESET = '\033[0m'

# Define the root of the project
ROOT_DIR = Path(__file__).resolve().parent.parent

# Define rules: (Rule Name, Regex Pattern to forbid, File extensions to check, Error message)
FORBIDDEN_PATTERNS = [
    (
        "PPO_REINFORCEMENT_LEARNING",
        re.compile(r'\b(ppo|proximal policy optimization|reinforcement learning)\b', re.IGNORECASE),
        ['.cpp', '.hpp', '.py', '.go'],
        "Continuous learning via PPO or RL is strictly forbidden. Use Forward-Forward algorithm instead."
    ),
    (
        "GRPC_OR_HTTP_SERVERS",
        re.compile(r'\b(grpc|net/http|flask|fastapi|django)\b', re.IGNORECASE),
        ['.go', '.py'],
        "Custom REST APIs and gRPC are forbidden. All agent and gateway communication MUST use JSON-RPC 2.0 over standard I/O (MCP Protocol)."
    ),
    (
        "DENSE_TABLEAU_TRACKING",
        re.compile(r'\b(gaussian elimination|dense matrix|O\(N\^3\))\b', re.IGNORECASE),
        ['.cpp', '.hpp'],
        "Dense tracking arrays are forbidden. Use QGNN Graph-State Standard Form (adjacency_matrix + vertex_operators)."
    ),
    (
        "FLASH_CIM_OVERRIDE",
        re.compile(r'\b(cim_override|flash_cim_tracking)\b', re.IGNORECASE),
        ['.cpp', '.hpp'],
        "Flash CIM cannot bypass the GF(3) stabilizer tableau. It must only be used as a 'Magic State Generator' injected into the Clifford tracking graph."
    )
]

# Directories to exclude from scanning (e.g. docs, venv, node_modules)
EXCLUDE_DIRS = {'.git', 'docs', 'venv', 'node_modules', 'build', '__pycache__', 'reports'}

def scan_file(file_path):
    violations = []
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            for line_num, line in enumerate(f, 1):
                for rule_name, pattern, extensions, message in FORBIDDEN_PATTERNS:
                    if file_path.suffix in extensions:
                        if pattern.search(line):
                            violations.append((rule_name, line_num, line.strip(), message))
    except Exception as e:
        print(f"{YELLOW}Warning: Could not read {file_path}. Error: {e}{RESET}")
    return violations

def enforce_architecture():
    print(f"{GREEN}Starting Architecture Enforcement Scan...{RESET}")
    
    total_violations = 0
    scanned_files = 0
    
    for root, dirs, files in os.walk(ROOT_DIR):
        # Exclude directories
        dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]
        
        for file in files:
            file_path = Path(root) / file
            
            # Skip the enforcement script itself and the rules file
            if file_path.name in ['enforce_architecture.py', '.clinerules']:
                continue
                
            # Only scan relevant extensions
            if file_path.suffix in ['.cpp', '.hpp', '.py', '.go']:
                scanned_files += 1
                violations = scan_file(file_path)
                
                if violations:
                    print(f"\n{RED}VIOLATION FOUND IN: {file_path.relative_to(ROOT_DIR)}{RESET}")
                    for rule_name, line_num, line, message in violations:
                        print(f"  {YELLOW}Line {line_num}:{RESET} {line}")
                        print(f"  {RED}Rule ({rule_name}):{RESET} {message}")
                        total_violations += 1

    print(f"\n{GREEN}Scan Complete.{RESET} Analyzed {scanned_files} files.")
    
    if total_violations > 0:
        print(f"\n{RED}FAILED: {total_violations} architectural violations found. Code cannot be merged.{RESET}")
        sys.exit(1)
    else:
        print(f"\n{GREEN}PASSED: No architectural violations found. Code is compliant with GF(3) and MCP protocols.{RESET}")
        sys.exit(0)

if __name__ == "__main__":
    enforce_architecture()
