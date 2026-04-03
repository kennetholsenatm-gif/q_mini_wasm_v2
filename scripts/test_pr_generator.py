#!/usr/bin/env python3
"""
Test script for Auto PR Generator
"""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

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


def test_pr_generation():
    """Test PR generation with sample data."""
    print("Testing Auto PR Generator...")
    print("-" * 50)
    
    # Test with sample data
    sample_commits = [
        {'short_hash': 'abc1234', 'subject': 'Add new feature', 'author': 'John', 'date': '2024-01-01'},
        {'short_hash': 'def5678', 'subject': 'Fix bug in module', 'author': 'Jane', 'date': '2024-01-02'}
    ]
    sample_files = {
        'added': ['new_feature.py', 'test_feature.py'],
        'modified': ['existing.py', 'config.json'],
        'deleted': ['old_file.py'],
        'renamed': ['old_name.py -> new_name.py']
    }
    sample_stats = {'files_changed': 4, 'insertions': 150, 'deletions': 30}
    
    print("\n1. Testing Markdown PR Body Generation:")
    print("-" * 50)
    md_output = generate_pr_body(
        sample_commits, 
        sample_files, 
        sample_stats, 
        'main', 
        'feature/new-feature',
        include_stats=True,
        include_commits=True
    )
    
    # Replace emojis with text for console output
    md_output_safe = md_output.replace('📋', '[PR]').replace('🎯', '[Target]').replace('📊', '[Stats]')
    md_output_safe = md_output_safe.replace('🆕', '[New]').replace('✏️', '[Modified]').replace('🗑️', '[Deleted]')
    md_output_safe = md_output_safe.replace('📝', '[Renamed]')
    
    print(md_output_safe)
    
    print("\n" + "=" * 50)
    print("2. Testing JSON Output Generation:")
    print("-" * 50)
    json_output = generate_json_output(sample_commits, sample_files, sample_stats)
    print(json_output)
    
    print("\n" + "=" * 50)
    print("All tests completed successfully!")


if __name__ == '__main__':
    test_pr_generation()