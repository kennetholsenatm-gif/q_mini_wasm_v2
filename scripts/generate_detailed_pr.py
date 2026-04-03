#!/usr/bin/env python3
"""
Auto PR Generator with Detailed Messages
=========================================

This script generates detailed pull request messages based on git changes.
It analyzes commits, file changes, and generates comprehensive PR descriptions.

Usage:
    python scripts/generate_detailed_pr.py [options]

Options:
    --branch NAME           Source branch name (default: current branch)
    --base NAME             Base branch name (default: main)
    --output FILE           Output file for PR body (default: stdout)
    --format FORMAT         Output format: markdown, json (default: markdown)
    --include-stats         Include file change statistics
    --include-commits       Include commit history
"""

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional


class GitAnalyzer:
    """Analyzes git repository for PR generation."""
    
    def __init__(self, repo_path: str = "."):
        self.repo_path = Path(repo_path).resolve()
        
    def run_git_command(self, args: List[str]) -> str:
        """Run a git command and return output."""
        try:
            result = subprocess.run(
                ["git"] + args,
                cwd=self.repo_path,
                capture_output=True,
                text=True,
                check=True
            )
            return result.stdout.strip()
        except subprocess.CalledProcessError as e:
            print(f"Git command failed: {e}", file=sys.stderr)
            return ""
    
    def get_current_branch(self) -> str:
        """Get current branch name."""
        return self.run_git_command(["rev-parse", "--abbrev-ref", "HEAD"])
    
    def get_commits_between(self, base: str, head: str) -> List[Dict]:
        """Get commits between two branches."""
        format_str = "%H|%h|%s|%an|%ae|%ai"
        output = self.run_git_command([
            "log", f"{base}..{head}", f"--pretty=format:{format_str}"
        ])
        
        if not output:
            return []
        
        commits = []
        for line in output.split('\n'):
            if '|' in line:
                parts = line.split('|', 5)
                if len(parts) == 6:
                    commits.append({
                        'hash': parts[0],
                        'short_hash': parts[1],
                        'subject': parts[2],
                        'author_name': parts[3],
                        'author_email': parts[4],
                        'date': parts[5]
                    })
        return commits
    
    def get_changed_files(self, base: str, head: str) -> Dict[str, List[str]]:
        """Get changed files between branches."""
        output = self.run_git_command(["diff", "--name-status", f"{base}...{head}"])
        
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
    
    def get_diff_stats(self, base: str, head: str) -> Dict:
        """Get diff statistics."""
        output = self.run_git_command(["diff", "--shortstat", f"{base}...{head}"])
        
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
    
    def get_file_categories(self, files: List[str]) -> Dict[str, List[str]]:
        """Categorize files by type."""
        categories = {
            'source': [],
            'tests': [],
            'docs': [],
            'config': [],
            'ci_cd': [],
            'scripts': [],
            'other': []
        }
        
        for file in files:
            path = Path(file)
            parts = path.parts
            file_lower = file.lower()
            
            if '.github' in parts or 'workflow' in file_lower:
                categories['ci_cd'].append(file)
            elif 'test' in file_lower or 'spec' in file_lower:
                categories['tests'].append(file)
            elif file.endswith(('.md', '.rst', '.txt')):
                categories['docs'].append(file)
            elif file.endswith(('.json', '.yml', '.yaml', '.toml', '.ini', '.cfg')):
                categories['config'].append(file)
            elif 'script' in parts or file.endswith('.sh'):
                categories['scripts'].append(file)
            elif file.endswith(('.py', '.js', '.ts', '.go', '.rs', '.cpp', '.c', '.h', '.hpp')):
                categories['source'].append(file)
            else:
                categories['other'].append(file)
        
        return {k: v for k, v in categories.items() if v}
    
    def generate_pr_body(self, base: str, head: str, 
                         include_stats: bool = True, include_commits: bool = True) -> str:
        """Generate markdown PR body."""
        
        commits = self.get_commits_between(base, head)
        changed_files = self.get_changed_files(base, head)
        stats = self.get_diff_stats(base, head)
        
        sections = []
        
        # Header
        sections.append("## 📋 Pull Request Summary\n")
        
        # Overview
        sections.append("### 🎯 Overview\n")
        if commits:
            sections.append(f"This PR includes **{len(commits)} commit(s)** from `{head}` to `{base}`.\n")
        
        # Statistics
        if include_stats and stats['files_changed'] > 0:
            sections.append("### 📊 Change Statistics\n")
            sections.append("| Metric | Count |")
            sections.append("|--------|-------|")
            sections.append(f"| Files Changed | {stats['files_changed']} |")
            sections.append(f"| Insertions | +{stats['insertions']} |")
            sections.append(f"| Deletions | -{stats['deletions']} |")
            sections.append("")
        
        # File changes
        emoji_map = {'added': '🆕', 'modified': '✏️', 'deleted': '🗑️', 'renamed': '📝'}
        for status, files in changed_files.items():
            if files:
                sections.append(f"### {emoji_map[status]} {status.title()} Files\n")
                for file in sorted(files)[:20]:
                    sections.append(f"- `{file}`")
                if len(files) > 20:
                    sections.append(f"- ... and {len(files) - 20} more files")
                sections.append("")
        
        # Commits
        if include_commits and commits:
            sections.append("### 📝 Commit History\n")
            sections.append("| Hash | Message | Author | Date |")
            sections.append("|------|---------|--------|------|")
            for commit in commits[:30]:
                short_msg = commit['subject'][:50] + ("..." if len(commit['subject']) > 50 else "")
                sections.append(f"| `{commit['short_hash']}` | {short_msg} | {commit['author_name']} | {commit['date']} |")
            sections.append("")
        
        # Footer
        sections.append("---")
        sections.append(f"*Generated on {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}*")
        
        return "\n".join(sections)
    
    def generate_json(self, base: str, head: str) -> str:
        """Generate JSON PR body."""
        
        commits = self.get_commits_between(base, head)
        changed_files = self.get_changed_files(base, head)
        stats = self.get_diff_stats(base, head)
        
        # Combine all changed files
        all_files = []
        for file_list in changed_files.values():
            all_files.extend(file_list)
        
        categories = self.get_file_categories(all_files)
        
        output = {
            "metadata": {
                "generated_at": datetime.now().isoformat(),
                "base_branch": base,
                "head_branch": head
            },
            "summary": {
                "commits": len(commits),
                "files_changed": stats['files_changed'],
                "insertions": stats['insertions'],
                "deletions": stats['deletions']
            },
            "files": changed_files,
            "categories": categories,
            "commits": commits
        }
        
        return json.dumps(output, indent=2)


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
    
    # Create analyzer
    analyzer = GitAnalyzer(args.repo_path)
    
    # Get branch names
    head_branch = args.branch or analyzer.get_current_branch()
    base_branch = args.base
    
    # Generate output
    if args.format == 'json':
        output = analyzer.generate_json(base_branch, head_branch)
    else:
        output = analyzer.generate_pr_body(
            base_branch,
            head_branch,
            include_stats=args.include_stats,
            include_commits=args.include_commits
        )
    
    # Output
    if args.output:
        with open(args.output, 'w', encoding='utf-8') as f:
            f.write(output)
        print(f"PR body written to {args.output}")
    else:
        print(output)


if __name__ == '__main__':
    main()