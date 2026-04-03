#!/usr/bin/env python3
"""
Auto PR Generator - Combined Script
"""

import argparse
import sys
from pathlib import Path

# Import functions from part 1 and part 2
from generate_pr_part1 import (
    get_current_branch,
    get_commits_between,
    get_changed_files,
    get_diff_stats
)

from generate_pr_part2 import (
    generate_pr_body,
    generate_json_output
)


def main():
    parser = argparse.ArgumentParser(description="Generate detailed PR body")
    parser.add_argument('--branch', help='Source branch (default: current)')
    parser.add_argument('--base', default='main', help='Base branch (default: main)')
    parser.add_argument('--output', help='Output file (default: stdout)')
    parser.add_argument('--format', choices=['markdown', 'json'], default='markdown')
    parser.add_argument('--include-stats', action='store_true')
    parser.add_argument('--include-commits', action='store_true')
    parser.add_argument('--repo-path', default='.')
    
    args = parser.parse_args()
    
    head_branch = args.branch or get_current_branch(args.repo_path)
    base_branch = args.base
    
    # Get data
    commits = get_commits_between(base_branch, head_branch, args.repo_path)
    changed_files = get_changed_files(base_branch, head_branch, args.repo_path)
    stats = get_diff_stats(base_branch, head_branch, args.repo_path)
    
    # Generate output
    if args.format == 'json':
        output = generate_json_output(commits, changed_files, stats)
    else:
        output = generate_pr_body(commits, changed_files, stats, base_branch, head_branch,
                                 args.include_stats, args.include_commits)
    
    # Output
    if args.output:
        with open(args.output, 'w') as f:
            f.write(output)
        print(f"PR body written to {args.output}")
    else:
        print(output)


if __name__ == '__main__':
    main()