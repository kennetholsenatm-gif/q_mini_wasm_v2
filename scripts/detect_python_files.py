#!/usr/bin/env python3
# Detect Python files and suggest rewrite targets.

import os
import json
import ast
from pathlib import Path
from typing import Dict, List, Any

def analyze_python_file(filepath: str) -> Dict[str, Any]:
    # Analyze a Python file and determine optimal rewrite target.
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    try:
        tree = ast.parse(content)
    except SyntaxError:
        return None
    analysis = {
        'file': filepath,
        'lines': len(content.split('\\n')),
        'classes': [],
        'functions': [],
        'imports': [],
        'async_functions': [],
        'has_regex': False,
        'has_async': False,
        'has_cli': False,
        'has_data': False,
        'target_language': 'r',
        'reason': ''
    }
    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef):
            analysis['classes'].append(node.name)
        elif isinstance(node, ast.FunctionDef):
            analysis['functions'].append(node.name)
        elif isinstance(node, ast.AsyncFunctionDef):
            analysis['async_functions'].append(node.name)
            analysis['has_async'] = True
        elif isinstance(node, ast.Import):
            for alias in node.names:
                analysis['imports'].append(alias.name)
        elif isinstance(node, ast.ImportFrom):
            if node.module:
                analysis['imports'].append(node.module)
    imports_str = ' '.join(analysis['imports']).lower()
    if 're' in imports_str or 'regex' in imports_str or 'cython' in imports_str:
        analysis['has_regex'] = True
    if 'click' in imports_str or 'argparse' in imports_str or 'sys.argv' in content.lower():
        analysis['has_cli'] = True
    if any(x in imports_str for x in ['pandas', 'numpy', 'scipy', 'sklearn', 'torch']):
        analysis['has_data'] = True
    if analysis['has_regex'] or 'linter' in filepath.lower() or 'parser' in filepath.lower():
        analysis['target_language'] = 'cpp'
        analysis['reason'] = 'Performance-critical regex/parsing'
    elif analysis['has_async'] or 'concurrent' in imports_str or 'threading' in imports_str:
        analysis['target_language'] = 'rust'
        analysis['reason'] = 'Concurrent/async systems programming'
    elif analysis['has_cli'] or 'cli' in filepath.lower() or 'http' in imports_str:
        analysis['target_language'] = 'go'
        analysis['reason'] = 'CLI/HTTP/network service'
    elif analysis['has_data'] or 'agent' in filepath.lower() or 'improvement' in filepath.lower():
        analysis['target_language'] = 'r'
        analysis['reason'] = 'Data analysis/auto-improvement infrastructure'
    elif 'cache' in filepath.lower() or 'rate' in filepath.lower():
        analysis['target_language'] = 'rust'
        analysis['reason'] = 'Rate limiting/caching'
    else:
        if 'agents/' in filepath:
            analysis['target_language'] = 'r'
            analysis['reason'] = 'Auto-improvement infrastructure'
        elif 'docs/' in filepath:
            analysis['target_language'] = 'cpp'
            analysis['reason'] = 'Documentation processing'
    return analysis

def main():
    python_files = []
    for root, dirs, files in os.walk('.'):
        dirs[:] = [d for d in dirs if d not in ['__pycache__', '.git', 'node_modules', 'venv', 'build']]
        for file in files:
            if file.endswith('.py'):
                python_files.append(os.path.join(root, file))
    analyses = []
    for filepath in python_files:
        analysis = analyze_python_file(filepath)
        if analysis:
            analyses.append(analysis)
    os.makedirs('reports', exist_ok=True)
    with open('reports/python_analysis.json', 'w') as f:
        json.dump(analyses, f, indent=2)
    print(f'Analyzed {len(analyses)} Python files')
    by_target = {}
    for a in analyses:
        target = a['target_language']
        if target not in by_target:
            by_target[target] = []
        by_target[target].append(a['file'])
    for target, files in sorted(by_target.items()):
        print(f'{target.upper()}: {len(files)} files')
        for f in sorted(files):
            print(f'  - {f}')

if __name__ == '__main__':
    main()
