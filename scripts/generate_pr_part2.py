#!/usr/bin/env python3
"""
Auto PR Generator - Part 2: PR Body Generation
"""

import json
from datetime import datetime
from typing import Dict, List


def generate_pr_body(commits: List[Dict], changed_files: Dict[str, List[str]], 
                    stats: Dict, base: str, head: str,
                    include_stats: bool = True, include_commits: bool = True) -> str:
    """Generate markdown PR body."""
    
    sections = []
    
    sections.append("## 📋 Pull Request Summary\n")
    sections.append("### 🎯 Overview\n")
    
    if commits:
        sections.append(f"This PR includes **{len(commits)} commit(s)** from `{head}` to `{base}`.\n")
    
    if include_stats and stats['files_changed'] > 0:
        sections.append("### 📊 Change Statistics\n")
        sections.append("| Metric | Count |")
        sections.append("|--------|-------|")
        sections.append(f"| Files Changed | {stats['files_changed']} |")
        sections.append(f"| Insertions | +{stats['insertions']} |")
        sections.append(f"| Deletions | -{stats['deletions']} |")
        sections.append("")
    
    emoji_map = {'added': '🆕', 'modified': '✏️', 'deleted': '🗑️', 'renamed': '📝'}
    for status, files in changed_files.items():
        if files:
            sections.append(f"### {emoji_map[status]} {status.title()} Files\n")
            for file in sorted(files)[:20]:
                sections.append(f"- `{file}`")
            if len(files) > 20:
                sections.append(f"- ... and {len(files) - 20} more files")
            sections.append("")
    
    if include_commits and commits:
        sections.append("### 📝 Commit History\n")
        sections.append("| Hash | Message | Author | Date |")
        sections.append("|------|---------|--------|------|")
        for commit in commits[:30]:
            short_msg = commit['subject'][:50] + ("..." if len(commit['subject']) > 50 else "")
            sections.append(f"| `{commit['short_hash']}` | {short_msg} | {commit['author']} | {commit['date']} |")
        sections.append("")
    
    sections.append("---")
    sections.append(f"*Generated on {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}*")
    
    return "\n".join(sections)


def generate_json_output(commits: List[Dict], changed_files: Dict[str, List[str]], 
                        stats: Dict) -> str:
    """Generate JSON output."""
    return json.dumps({
        'metadata': {'generated_at': datetime.now().isoformat()},
        'summary': {'commits': len(commits), **stats},
        'files': changed_files,
        'commits': commits
    }, indent=2)


if __name__ == '__main__':
    # Test with sample data
    sample_commits = [
        {'short_hash': 'abc1234', 'subject': 'Add new feature', 'author': 'John', 'date': '2024-01-01'},
        {'short_hash': 'def5678', 'subject': 'Fix bug', 'author': 'Jane', 'date': '2024-01-02'}
    ]
    sample_files = {'added': ['new_file.py'], 'modified': ['existing.py'], 'deleted': [], 'renamed': []}
    sample_stats = {'files_changed': 2, 'insertions': 50, 'deletions': 10}
    
    print(generate_pr_body(sample_commits, sample_files, sample_stats, 'main', 'feature'))