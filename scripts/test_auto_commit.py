#!/usr/bin/env python3
"""
Test script for the auto-commit functionality
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'scripts'))

from auto_commit import (
    analyze_file_changes,
    determine_commit_type,
    generate_scope,
    generate_commit_message,
    validate_commit_message
)


def test_analyze_file_changes():
    """Test file categorization"""
    print("Testing analyze_file_changes...")
    
    test_files = [
        "src/quantum_core/main.cpp",
        "tests/test_quantum.py",
        "docs/README.md",
        "config/settings.json",
        "Makefile",
        "scripts/deploy.sh"
    ]
    
    categories = analyze_file_changes(test_files)
    
    assert len(categories["source"]) == 1, f"Expected 1 source file, got {len(categories['source'])}"
    assert len(categories["test"]) == 1, f"Expected 1 test file, got {len(categories['test'])}"
    assert len(categories["docs"]) == 1, f"Expected 1 docs file, got {len(categories['docs'])}"
    assert len(categories["config"]) == 1, f"Expected 1 config file, got {len(categories['config'])}"
    assert len(categories["build"]) == 1, f"Expected 1 build file, got {len(categories['build'])}"
    
    print("✅ analyze_file_changes passed")


def test_determine_commit_type():
    """Test commit type determination"""
    print("Testing determine_commit_type...")
    
    # Test branch-based detection
    categories = {"source": [], "test": [], "docs": [], "config": [], "build": [], "other": []}
    
    assert determine_commit_type(categories, "feature/new-feature") == "feat"
    assert determine_commit_type(categories, "fix/bug-fix") == "fix"
    assert determine_commit_type(categories, "docs/update-readme") == "docs"
    assert determine_commit_type(categories, "refactor/cleanup") == "refactor"
    assert determine_commit_type(categories, "test/add-tests") == "test"
    
    # Test file-based detection
    categories["test"] = ["test_file.py"]
    assert determine_commit_type(categories, "main") == "test"
    
    categories = {"source": [], "test": [], "docs": ["README.md"], "config": [], "build": [], "other": []}
    assert determine_commit_type(categories, "main") == "docs"
    
    categories = {"source": ["main.cpp"], "test": [], "docs": [], "config": [], "build": [], "other": []}
    assert determine_commit_type(categories, "main") == "feat"
    
    print("✅ determine_commit_type passed")


def test_generate_scope():
    """Test scope generation"""
    print("Testing generate_scope...")
    
    categories = {
        "source": ["src/quantum_core/main.cpp"],
        "test": [],
        "docs": [],
        "config": ["config/settings.json"],
        "build": [],
        "other": []
    }
    
    scope = generate_scope(categories)
    assert "quantum_core" in scope or "config" in scope, f"Unexpected scope: {scope}"
    
    print("✅ generate_scope passed")


def test_generate_commit_message():
    """Test commit message generation"""
    print("Testing generate_commit_message...")
    
    categories = {
        "source": ["main.cpp"],
        "test": [],
        "docs": [],
        "config": [],
        "build": [],
        "other": []
    }
    stats = {"files": 1, "insertions": 50, "deletions": 10}
    
    message = generate_commit_message("feat", "quantum_core", stats, categories)
    assert message.startswith("feat(quantum_core):"), f"Unexpected message format: {message}"
    
    message = generate_commit_message("fix", "", stats, categories)
    assert message.startswith("fix:"), f"Unexpected message format: {message}"
    
    print("✅ generate_commit_message passed")


def test_validate_commit_message():
    """Test commit message validation"""
    print("Testing validate_commit_message...")
    
    # Valid messages
    valid, msg = validate_commit_message("feat: add new feature")
    assert valid, f"Expected valid, got: {msg}"
    
    valid, msg = validate_commit_message("fix(module): fix bug in module")
    assert valid, f"Expected valid, got: {msg}"
    
    valid, msg = validate_commit_message("quantum(quantum_core): update quantum operations")
    assert valid, f"Expected valid, got: {msg}"
    
    # Invalid messages
    valid, msg = validate_commit_message("invalid message")
    assert not valid, f"Expected invalid, got: {msg}"
    
    valid, msg = validate_commit_message("feat: ")
    assert not valid, f"Expected invalid, got: {msg}"
    
    # Too long message
    long_msg = "feat: " + "a" * 100
    valid, msg = validate_commit_message(long_msg)
    assert not valid, f"Expected invalid for long message, got: {msg}"
    
    print("✅ validate_commit_message passed")


def main():
    """Run all tests"""
    print("Running auto-commit tests...\n")
    
    try:
        test_analyze_file_changes()
        test_determine_commit_type()
        test_generate_scope()
        test_generate_commit_message()
        test_validate_commit_message()
        
        print("\n✅ All tests passed!")
        return 0
    except AssertionError as e:
        print(f"\n❌ Test failed: {e}")
        return 1
    except Exception as e:
        print(f"\n❌ Unexpected error: {e}")
        return 1


if __name__ == '__main__':
    sys.exit(main())