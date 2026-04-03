#!/usr/bin/env python3
"""
Environment Scanner Utility

This script scans directories for environment files and variables,
helping to identify environment secrets paths across projects.
"""

import os
import sys
import json
from pathlib import Path
from typing import Dict, List, Any
import re

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from agents.config import env_config, find_all_env_files, scan_for_env_variables


def scan_directory_for_env_files(directory: str) -> List[Dict[str, Any]]:
    """
    Scan a directory for environment files and variables.
    
    Args:
        directory: Directory to scan
        
    Returns:
        List of environment file information
    """
    directory_path = Path(directory)
    if not directory_path.exists():
        print(f"Directory not found: {directory}")
        return []
    
    print(f"Scanning directory: {directory}")
    print("=" * 60)
    
    # Find all .env files
    env_files = find_all_env_files(directory_path)
    
    results = []
    
    for env_file in env_files:
        try:
            # Read file content
            content = env_file.read_text(encoding="utf-8")
            
            # Parse environment variables
            env_vars = {}
            for line in content.splitlines():
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    key, value = line.split("=", 1)
                    env_vars[key.strip()] = value.strip()
            
            # Get relative path
            rel_path = env_file.relative_to(directory_path)
            
            results.append({
                "file": str(env_file),
                "relative_path": str(rel_path),
                "exists": True,
                "variable_count": len(env_vars),
                "variables": list(env_vars.keys()),
                "size_bytes": env_file.stat().st_size
            })
            
            print(f"\n📄 Found: {rel_path}")
            print(f"   Variables: {len(env_vars)}")
            print(f"   Size: {env_file.stat().st_size} bytes")
            
            # Show first few variables (without values)
            if env_vars:
                print(f"   Sample variables:")
                for i, key in enumerate(list(env_vars.keys())[:5]):
                    print(f"     - {key}")
                if len(env_vars) > 5:
                    print(f"     ... and {len(env_vars) - 5} more")
                    
        except Exception as e:
            print(f"\n⚠️  Error reading {env_file}: {e}")
    
    return results


def generate_env_report(directory: str, output_file: str = None) -> None:
    """
    Generate a comprehensive environment report.
    
    Args:
        directory: Directory to scan
        output_file: Optional output file for JSON report
    """
    print("\n" + "="*60)
    print("ENVIRONMENT SECRETS PATH FINDER")
    print("="*60)
    print(f"\nTarget directory: {directory}")
    print(f"Generated at: {os.getcwd()}")
    
    # Scan for environment files
    env_files = scan_directory_for_env_files(directory)
    
    # Scan for environment variable usage
    env_vars_by_file = scan_for_env_variables(Path(directory))
    
    # Compile all unique environment variables
    all_env_vars = set()
    for vars_list in env_vars_by_file.values():
        all_env_vars.update(vars_list)
    
    # Generate summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print("="*60)
    print(f"Environment files found: {len(env_files)}")
    print(f"Code files using environment variables: {len(env_vars_by_file)}")
    print(f"Unique environment variables: {len(all_env_vars)}")
    
    # Show common environment variables
    if all_env_vars:
        print(f"\nCommon environment variables found:")
        for var in sorted(all_env_vars):
            print(f"  - {var}")
    
    # Generate JSON report if output file specified
    if output_file:
        report = {
            "scan_directory": directory,
            "scan_timestamp": str(os.getcwd()),
            "environment_files": env_files,
            "code_files_using_env_vars": {
                str(k.relative_to(directory)): v 
                for k, v in env_vars_by_file.items()
            },
            "all_environment_variables": sorted(list(all_env_vars)),
            "summary": {
                "env_files_count": len(env_files),
                "code_files_count": len(env_vars_by_file),
                "unique_vars_count": len(all_env_vars)
            }
        }
        
        output_path = Path(output_file)
        output_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"\n📄 JSON report saved to: {output_file}")
    
    print(f"\n{'='*60}")
    print("SCAN COMPLETE")
    print("="*60)


def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Scan directories for environment files and variables"
    )
    parser.add_argument(
        "directory",
        nargs="?",
        default=".",
        help="Directory to scan (default: current directory)"
    )
    parser.add_argument(
        "--output", "-o",
        help="Output file for JSON report"
    )
    parser.add_argument(
        "--github",
        action="store_true",
        help="Scan C:\\GitHub directory (if it exists)"
    )
    
    args = parser.parse_args()
    
    if args.github:
        github_dir = Path("C:\\GitHub")
        if github_dir.exists():
            scan_directory = str(github_dir)
        else:
            print(f"C:\\GitHub directory not found. Using current directory.")
            scan_directory = args.directory
    else:
        scan_directory = args.directory
    
    generate_env_report(scan_directory, args.output)


if __name__ == "__main__":
    main()