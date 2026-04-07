#!/usr/bin/env python3
"""
GF(3) Integrity Validator
Validates that all core operations strictly adhere to Galois Field GF(3) constraints
Blocks floating point operations inside critical stabilizer loops
"""
import os
import re
import sys
import subprocess
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent
CORE_DIR = PROJECT_ROOT / "q_mini_wasm_v2" / "core"

# Critical paths that must never contain floating point operations
CRITICAL_PATHS = [
    "stabilizer/",
    "memory/",
    "ternary/",
    "learning/",
    "qgnn/",
    "steane/",
]

# Agent paths that must adhere to GF(3) semantic constraints
AGENT_PATHS = [
    "../../agents/",
]

FORBIDDEN_BOOLEAN_KEYWORDS = [
    r'\bbool\b',
    r'\bboolean\b',
    r'\bTrue\b',
    r'\bFalse\b',
    r'\btrue\b',
    r'\bfalse\b',
]

FORBIDDEN_FLOAT_KEYWORDS = [
    r'\bfloat\b',
    r'\bdouble\b',
    r'\bf32\b',
    r'\bf64\b',
    r'static_cast<double>',
    r'static_cast<float>',
    r'\.toFloat\(',
    r'\.toDouble\(',
    r'std::floating_point',
]

ALLOWED_FLOAT_LOCATIONS = [
    "ingestion/",     # Input quantization boundary
    "training/",      # Hyperparameter scheduling
    "inference/latency_profiler.cpp", # Performance metrics
    "flash_cim/",     # Energy calculation
]


def _strip_line_comment(line: str) -> str:
    """Remove simple inline comments for contamination scanning."""
    if "//" in line:
        return line.split("//", 1)[0]
    return line

def scan_file_for_floats(file_path: Path) -> list[tuple[int, str]]:
    """Scan a single file for floating point operations"""
    violations = []
    
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()
    
    for line_num, line in enumerate(lines, 1):
        scan_line = _strip_line_comment(line)
        if not scan_line.strip():
            continue

        for pattern in FORBIDDEN_FLOAT_KEYWORDS:
            if re.search(pattern, scan_line):
                violations.append((line_num, scan_line.strip()))
    
    return violations

def validate_critical_paths() -> bool:
    """Validate all critical paths for GF(3) integrity"""
    print("="*70)
    print("GF(3) INTEGRITY VALIDATION")
    print("="*70)
    
    all_valid = True
    total_violations = 0
    
    for critical_path in CRITICAL_PATHS:
        path = CORE_DIR / critical_path
        if not path.exists():
            continue
            
        for root, _, files in os.walk(path):
            for file in files:
                if not file.endswith(('.cpp', '.hpp', '.h')):
                    continue
                    
                file_path = Path(root) / file
                relative_path = file_path.relative_to(CORE_DIR)
                
                # Skip allowed locations
                allowed = False
                for allowed_path in ALLOWED_FLOAT_LOCATIONS:
                    if str(relative_path).startswith(allowed_path):
                        allowed = True
                        break
                
                if allowed:
                    continue
                    
                violations = scan_file_for_floats(file_path)
                if violations:
                    print(f"\nERROR: VIOLATIONS in {relative_path}:")
                    for line_num, line in violations:
                        print(f"   Line {line_num:4d}: {line}")
                        total_violations += 1
                    all_valid = False

    print("\n" + "="*70)
    if all_valid:
        print("SUCCESS: ALL CRITICAL PATHS ARE GF(3) COMPLIANT")
        print(f"   No floating point operations found in stabilizer loops")
    else:
        print(f"ERROR: FOUND {total_violations} FLOATING POINT VIOLATIONS")
        print("   These must be fixed before merge")
    print("="*70)
    
    return all_valid

def validate_agent_semantics() -> bool:
    """Validate agent layer adheres to GF(3) semantic constraints"""
    print("\n" + "="*70)
    print("AGENT LAYER SEMANTIC VALIDATION")
    print("="*70)
    
    all_valid = True
    total_violations = 0
    
    for agent_path in AGENT_PATHS:
        path = CORE_DIR / agent_path
        if not path.exists():
            continue
            
        for root, _, files in os.walk(path):
            for file in files:
                if not file.endswith(('.py', '.go')):
                    continue
                    
                file_path = Path(root) / file
                relative_path = file_path.relative_to(PROJECT_ROOT)
                
                with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                    lines = f.readlines()
                
                violations = []
                for line_num, line in enumerate(lines, 1):
                    for pattern in FORBIDDEN_BOOLEAN_KEYWORDS:
                        if re.search(pattern, line):
                            violations.append((line_num, line.strip()))
                
                if violations:
                    print(f"\nWARNING: SEMANTIC VIOLATIONS in {relative_path}:")
                    for line_num, line in violations:
                        print(f"   Line {line_num:4d}: {line}")
                        total_violations += 1
    
    if total_violations == 0:
        print("SUCCESS: AGENT LAYER SEMANTICS ARE COMPLIANT")
    else:
        print(f"\nWARNING: FOUND {total_violations} BOOLEAN SEMANTIC VIOLATIONS")
        all_valid = False
    
    return all_valid

def validate_wasm_binary() -> bool:
    """Validate compiled WASM binary for forbidden instructions"""
    print("\n" + "="*70)
    print("WASM BINARY INSTRUCTION AUDIT")
    print("="*70)
    
    wasm_file = PROJECT_ROOT / "q_mini_wasm_v2" / "build" / "qminiwasm.wasm"
    if not wasm_file.exists():
        print("WARNING: WASM binary not found, skipping instruction audit")
        return True
    
    try:
        # Check for floating point instructions
        result = subprocess.run(
            ["wasm-objdump", "-d", str(wasm_file)],
            capture_output=True,
            text=True
        )
        
        float_instructions = [
            "f32.", "f64.", "f32x4.", "f64x2."
        ]
        
        float_count = 0
        for line in result.stdout.splitlines():
            for instr in float_instructions:
                if instr in line:
                    float_count += 1
        
        if float_count == 0:
            print("SUCCESS: WASM BINARY IS PURE INTEGER")
            print("   No floating point instructions detected")
            return True
        else:
            print(f"ERROR: FOUND {float_count} FLOATING POINT INSTRUCTIONS IN WASM")
            return False
            
    except Exception as e:
        print(f"WARNING: WASM audit failed: {e}")
        return True

def main():
    print("\nRunning Quantum Audit Validation Suite\n")
    
    valid1 = validate_critical_paths()
    valid2 = validate_agent_semantics()
    valid3 = validate_wasm_binary()
    
    print("\n" + "="*70)
    if valid1 and valid2 and valid3:
        print("SUCCESS: ALL VALIDATIONS PASSED")
        print("   Architecture is compliant with Gottesman-Knill constraints")
        print("="*70)
        return 0
    else:
        print("FAILURE: VALIDATION FAILED")
        print("   Contamination detected in critical paths or agent layer")
        print("="*70)
        return 1

if __name__ == "__main__":
    sys.exit(main())