#!/usr/bin/env python3
"""
Auto PR Generator - Combined Script

This script wraps the generate_detailed_pr.py functionality.
"""

import subprocess
import sys
from pathlib import Path


def main():
    """Run the generate_detailed_pr.py script."""
    script_path = Path(__file__).parent / "generate_detailed_pr.py"
    
    if not script_path.exists():
        print(f"Error: {script_path} not found", file=sys.stderr)
        sys.exit(1)
    
    # Pass all arguments to the detailed script
    result = subprocess.run(
        [sys.executable, str(script_path)] + sys.argv[1:],
        capture_output=False
    )
    sys.exit(result.returncode)


if __name__ == '__main__':
    main()