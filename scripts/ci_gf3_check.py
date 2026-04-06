#!/usr/bin/env python3
"""
CI/CD GF(3) Validation Integration
Automated checking for continuous integration
"""

import os
import sys
import json
import subprocess
from pathlib import Path
from typing import Dict, List

def run_gf3_validation(source_dir: Path) -> bool:
    """Run GF(3) validation"""
    print("🔍 Running GF(3) validation...")
    
    try:
        result = subprocess.run([
            sys.executable, 
            "scripts/validate_gf3.py",
            str(source_dir),
            "--recursive",
            "--completeness",
            "--energy"
        ], capture_output=True, text=True, timeout=300)
        
        print(result.stdout)
        if result.stderr:
            print("STDERR:", result.stderr)
        
        return result.returncode == 0
    except subprocess.TimeoutExpired:
        print("❌ GF(3) validation timed out")
        return False
    except Exception as e:
        print(f"❌ GF(3) validation failed: {e}")
        return False

def run_energy_efficiency_test(source_dir: Path, build_dir: Path = None) -> bool:
    """Run energy efficiency tests"""
    print("⚡ Running energy efficiency tests...")
    
    cmd = [sys.executable, "scripts/energy_efficiency_test.py", str(source_dir)]
    if build_dir and build_dir.exists():
        cmd.extend(["--build-dir", str(build_dir)])
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        
        print(result.stdout)
        if result.stderr:
            print("STDERR:", result.stderr)
        
        return result.returncode == 0
    except subprocess.TimeoutExpired:
        print("❌ Energy efficiency test timed out")
        return False
    except Exception as e:
        print(f"❌ Energy efficiency test failed: {e}")
        return False

def check_binary_pollution_quick(source_dir: Path) -> bool:
    """Quick check for obvious binary pollution"""
    print("⚡ Quick binary pollution check...")
    
    binary_patterns = [
        r'\bdouble\b',
        r'\bfloat\b',
        r'\bstd::vector<double\b',
        r'\bstd::mt19937\b',
        r'\bstd::normal_distribution\b'
    ]
    
    cpp_files = list(source_dir.rglob("*.cpp")) + list(source_dir.rglob("*.hpp"))
    
    for file_path in cpp_files:
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
                lines = f.readlines()
        except:
            continue
        
        for line_num, line in enumerate(lines, 1):
            line_stripped = line.strip()
            
            # Skip comments
            if line_stripped.startswith('//') or line_stripped.startswith('*'):
                continue
            
            for pattern in binary_patterns:
                import re
                if re.search(pattern, line):
                    print(f"❌ Binary pollution found: {file_path}:{line_num} - {pattern}")
                    return False
    
    print("✅ No obvious binary pollution found")
    return True

def main():
    repo_root = Path(__file__).parent.parent
    source_dir = repo_root / "q_mini_wasm_v2" / "core"
    build_dir = repo_root / "q_mini_wasm_v2" / "build"
    
    print("🚀 CI/CD GF(3) Validation Pipeline")
    print("=" * 50)
    
    all_passed = True
    
    # Quick check first
    if not check_binary_pollution_quick(source_dir):
        all_passed = False
    
    # Full GF(3) validation
    if not run_gf3_validation(source_dir):
        all_passed = False
    
    # Energy efficiency tests
    if not run_energy_efficiency_test(source_dir, build_dir):
        all_passed = False
    
    print("\n" + "=" * 50)
    if all_passed:
        print("✅ All GF(3) validation checks passed!")
        sys.exit(0)
    else:
        print("❌ Some GF(3) validation checks failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
