# Wiki Population Guide

## Problem
The GitHub Wiki is empty despite having extensive research documentation in `docs/research/`.

## Solution
The wiki needs to be generated from the documentation. There are two ways to do this:

### Option 1: Run the Python Script
```bash
python generate_wiki_simple.py
```

### Option 2: Run the Original Wiki Generator
```bash
python docs/wiki-pipeline/generate_wiki.py
```

## What Gets Generated
The wiki will include:

### Research Papers (from docs/research/)
1. **Cognitive Ergonomics Model Protocol** - WUI design principles
2. **Enhancing Framework with Clifford Entanglement** - Qutrit stabilizer formalism  
3. **QMINIWASM Quantum-Classical Framework Synthesis** - Unified architecture

### Architecture Documentation (from docs/architecture/)
- Overview
- Ternary State Space
- Stabilizer Tableau
- MoE Routing
- Forward-Forward Learning
- SYCL Acceleration

### API Reference (from docs/api/)
- Core API Reference

### Guides (from docs/guides/)
- Building Guide
- SYCL Setup Guide
- Contributing Guide

### Decisions (from docs/decisions/)
- ADR-001: Ternary Over Binary

## After Generation
1. The `wiki-output/` directory will be created with all wiki files
2. Push these files to your GitHub Wiki repository:
   ```bash
   git clone https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.wiki.git
   cp wiki-output/* q_mini_wasm_v2.wiki/
   cd q_mini_wasm_v2.wiki
   git add .
   git commit -m "Populate wiki with research documentation"
   git push
   ```

## Files Already Created
- `Home.md` - Main wiki page
- `_Sidebar.md` - Navigation sidebar
- `generate_wiki_simple.py` - Simple Python script to generate wiki
- `generate-wiki.ps1` - PowerShell script for Windows

## Manual Alternative
If scripts don't work, manually copy files from `docs/` to `wiki-output/`:
1. Create `wiki-output/` directory
2. Copy `README.md` to `wiki-output/Home.md`
3. Copy architecture docs with renamed files
4. Copy research papers with renamed files
5. Copy other documentation