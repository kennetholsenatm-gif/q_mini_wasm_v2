#!/usr/bin/env python3
"""
Auto PR Generator - Part 1: Core Functions
"""

import argparse
import json
import subprocess
from datetime import datetime
from pathlib import Path
from typing import Dict, List


def run_git(args: List[str], repo_path: str = ".") -> str:
    try:
        result = subprocess.run(
            ["git"] + args,
            cwd=Path(repo_path).resolve(),
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError:
        return ""


def get_current_branch(repo_path: str = ".") -> str:
    return run_git(["rev-parse", "--abbrev-ref", "HEAD"], repo_path)


def get_commits_between(base: str, head: str, repo_path: str = ".") -> List[Dict]:
    format_str = "%H|%h|%s|%an|%ai"
    output = run_git(["log", f"{base}..{head}", f"--pretty=format:{format_str}"], repo_path)
    
    if not output:
        return []
    
    commits = []
    for line in output.split('\n'):
        if '|' in line:
            parts = line.split('|', 4)
            if len(parts) == 5:
                commits.append({
                    'hash': parts[0],
                    'short_hash': parts[1],
                    'subject': parts[2],
                    'author': parts[3],
                    'date': parts[4][:10]
                })
    return commits


def get_changed_files(base: str, head: str, repo_path: str = ".") -> Dict[str, List[str]]:
    output = run_git(["diff", "--name-status", f"{base}...{head}"], repo_path)
    
    files = {'added': [], 'modified': [], 'deleted': [], 'renamed': []}
    
    if not output:
        return files
    
    for line in output.split('\n'):
        if not line:
            continue
        parts = line.split('\t')
        status = parts[0]
        
        if status == 'A' and len(parts) >= 2:
            files['added'].append(parts[1])
        elif status == 'M' and len(parts) >= 2:
            files['modified'].append(parts[1])
        elif status == 'D' and len(parts) >= 2:
            files['deleted'].append(parts[1])
        elif status.startswith('R') and len(parts) >= 3:
            files['renamed'].append(f"{parts[1]} → {parts[2]}")
    
    return files


def get_diff_stats(base: str, head: str, repo_path: str = ".") -> Dict:
    output = run_git(["diff", "--shortstat", f"{base}...{head}"], repo_path)
    
    if not output:
        return {'files_changed': 0, 'insertions': 0, 'deletions': 0}
    
    stats = {'files_changed': 0, 'insertions': 0, 'deletions': 0}
    
    parts = output.split(',')
    for part in parts:
        part = part.strip()
        if 'file' in part:
            stats['files_changed'] = int(''.join(filter(str.isdigit, part.split()[0])))
        elif 'insertion' in part:
            stats['insertions'] = int(''.join(filter(str.isdigit, part.split()[0])))
        elif 'deletion' in part:
            stats['deletions'] = int(''.join(filter(str.isdigit, part.split()[0])))
    
    return stats