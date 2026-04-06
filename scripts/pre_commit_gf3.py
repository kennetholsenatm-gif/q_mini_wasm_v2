#!/usr/bin/env python3
"""
Pre-commit hook for GF(3) validation
Ensures code purity before commits
"""

import sys
import subprocess
from pathlib import Path

def main():
    repo_root = Path(__file__).parent.parent
    source_dir = repo_root / "q_mini_wasm_v2" / "core"
    
    print("🔍 Pre-commit GF(3) Validation")
    print("=" * 40)
    
    # Quick validation
    try:
        # Run GF(3) validation
        result = subprocess.run([
            sys.executable, 
            "scripts/validate_gf3.py",
            str(source_dir),
            "--recursive"
        ], capture_output=True, text=True)
        
        if result.returncode != 0:
            print("❌ GF(3) validation failed!")
            print(result.stdout)
            if result.stderr:
                print("STDERR:", result.stderr)
            print("\n💡 Fix binary pollution before committing")
            sys.exit(1)
        
        print("✅ GF(3) validation passed!")
        
        # Check for obvious issues
        result = subprocess.run([
            sys.executable, 
            "scripts/ci_gf3_check.py"
        ], capture_output=True, text=True)
        
        if result.returncode != 0:
            print("❌ CI validation failed!")
            print(result.stdout)
            sys.exit(1)
        
        print("✅ Ready to commit!")
        sys.exit(0)
        
    except Exception as e:
        print(f"❌ Pre-commit validation error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
