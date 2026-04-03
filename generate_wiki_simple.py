#!/usr/bin/env python3
"""
Simple Wiki Generator for q_mini_wasm_v2
Run this script to generate wiki-output/ directory with all documentation
"""

import os
import shutil
from pathlib import Path

REPO_ROOT = Path(__file__).parent
DOCS_DIR = REPO_ROOT / "docs"
OUTPUT_DIR = REPO_ROOT / "wiki-output"

def main():
    print("Generating wiki from docs/...")
    
    # Clean output
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    
    # Copy Home.md from README
    readme = REPO_ROOT / "README.md"
    if readme.exists():
        shutil.copy(readme, OUTPUT_DIR / "Home.md")
    
    # Create sidebar
    sidebar = """* [Home](Home.md)
**Architecture**
  * [Overview](Architecture-Overview.md)
  * [Ternary State Space](Architecture-Ternary-State-Space.md)
  * [Stabilizer Tableau](Architecture-Stabilizer-Tableau.md)
  * [MoE Routing](Architecture-MoE-Routing.md)
  * [Forward-Forward](Architecture-Forward-Forward.md)
  * [SYCL Acceleration](Architecture-SYCL-Acceleration.md)
**API Reference**
  * [Core API](API-Core-Reference.md)
**Guides**
  * [Building](Guides-Building.md)
  * [SYCL Setup](Guides-SYCL-Setup.md)
  * [Contributing](Guides-Contributing.md)
**Research**
  * [Cognitive Ergonomics](Research-Cognitive-Ergonomics.md)
  * [Clifford Entanglement](Research-Clifford-Entanglement.md)
  * [Framework Synthesis](Research-Framework-Synthesis.md)
**Decisions**
  * [ADR-001: Ternary](Decisions-ADR-001-Ternary.md)
"""
    (OUTPUT_DIR / "_Sidebar.md").write_text(sidebar, encoding="utf-8")
    
    # Copy architecture docs
    arch_mapping = {
        "overview.md": "Architecture-Overview.md",
        "ternary-state-space.md": "Architecture-Ternary-State-Space.md",
        "stabilizer-tableau.md": "Architecture-Stabilizer-Tableau.md",
        "moe-routing.md": "Architecture-MoE-Routing.md",
        "forward-forward.md": "Architecture-Forward-Forward.md",
        "sycl-acceleration.md": "Architecture-SYCL-Acceleration.md",
    }
    for src_name, wiki_name in arch_mapping.items():
        src = DOCS_DIR / "architecture" / src_name
        if src.exists():
            shutil.copy(src, OUTPUT_DIR / wiki_name)
            print(f"  Created: {wiki_name}")
    
    # Copy API docs
    api_src = DOCS_DIR / "api" / "core-reference.md"
    if api_src.exists():
        shutil.copy(api_src, OUTPUT_DIR / "API-Core-Reference.md")
        print("  Created: API-Core-Reference.md")
    
    # Copy guides
    guide_mapping = {
        "building.md": "Guides-Building.md",
        "sycl-setup.md": "Guides-SYCL-Setup.md",
        "contributing.md": "Guides-Contributing.md",
    }
    for src_name, wiki_name in guide_mapping.items():
        src = DOCS_DIR / "guides" / src_name
        if src.exists():
            shutil.copy(src, OUTPUT_DIR / wiki_name)
            print(f"  Created: {wiki_name}")
    
    # Copy research papers
    research_mapping = {
        "Cognitive Ergonomics Model Protocol Research.md": "Research-Cognitive-Ergonomics.md",
        "Enhancing Framework with Clifford Entanglement.md": "Research-Clifford-Entanglement.md",
        "QMINIWASM_ Quantum-Classical Framework Synthesis.md": "Research-Framework-Synthesis.md",
    }
    for src_name, wiki_name in research_mapping.items():
        src = DOCS_DIR / "research" / src_name
        if src.exists():
            shutil.copy(src, OUTPUT_DIR / wiki_name)
            print(f"  Created: {wiki_name}")
    
    # Copy decisions
    adr_src = DOCS_DIR / "decisions" / "adr-001-ternary-over-binary.md"
    if adr_src.exists():
        shutil.copy(adr_src, OUTPUT_DIR / "Decisions-ADR-001-Ternary.md")
        print("  Created: Decisions-ADR-001-Ternary.md")
    
    print(f"\nWiki generated: {len(list(OUTPUT_DIR.iterdir()))} pages -> {OUTPUT_DIR}")

if __name__ == "__main__":
    main()