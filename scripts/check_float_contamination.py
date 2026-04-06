#!/usr/bin/env python3
import os
import re
import sys
from pathlib import Path

# Directories that must be strictly GF(3) and cannot contain float/double
CORE_STRICT_DIRS = [
    'q_mini_wasm_v2/core/learning',
    'q_mini_wasm_v2/core/moe',
    'q_mini_wasm_v2/core/stabilizer',
    'q_mini_wasm_v2/core/qgnn',
    'q_mini_wasm_v2/core/ternary',
    'q_mini_wasm_v2/core/steane'
]

# Patterns for floating-point data types in C++
FLOAT_PATTERNS = [
    re.compile(r'\bfloat\b'),
    re.compile(r'\bdouble\b'),
    re.compile(r'\bstd::stod\b'),
    re.compile(r'\bstd::stof\b')
]

def scan_file(file_path):
    violations = []
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            for line_num, line in enumerate(f, 1):
                # Skip comments
                if '//' in line:
                    line = line.split('//')[0]
                for pattern in FLOAT_PATTERNS:
                    if pattern.search(line):
                        violations.append((line_num, line.strip()))
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return violations

def main():
    print("Running GF(3) Strict Float Contamination Linter...")
    total_violations = 0
    root = Path(__file__).resolve().parent.parent
    
    for strict_dir in CORE_STRICT_DIRS:
        dir_path = root / strict_dir
        if not dir_path.exists():
            continue
            
        for ext in ['*.cpp', '*.hpp', '*.h']:
            for file_path in dir_path.rglob(ext):
                violations = scan_file(file_path)
                if violations:
                    print(f"\n[VIOLATION] Floating-point contamination found in: {file_path.relative_to(root)}")
                    for line_num, line in violations:
                        print(f"  Line {line_num}: {line}")
                        total_violations += 1

    if total_violations > 0:
        print(f"\nFAILED: {total_violations} floating-point variables detected in strict GF(3) modules.")
        print("Please use integers, GF(3) modulo arithmetic, or fixed-point approximations.")
        sys.exit(1)
    else:
        print("\nPASSED: No floating-point contamination detected in strict GF(3) modules.")
        sys.exit(0)

if __name__ == "__main__":
    main()
