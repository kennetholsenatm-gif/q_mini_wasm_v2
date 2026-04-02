#!/usr/bin/env python3
"""
Wiki Generator for q_mini_wasm_v2

Converts docs/ directory structure into GitHub Wiki pages.
Follows Cognitive Ergonomics principles:
  - Miller's Law: max 7 sidebar categories
  - Progressive Disclosure: collapsible sections
  - Chunked content: short paragraphs, headers every 200 words
"""

import os
import shutil
import re
from pathlib import Path

REPO_ROOT = Path(__file__).parent.parent.parent
DOCS_DIR = REPO_ROOT / "docs"
OUTPUT_DIR = REPO_ROOT / "wiki-output"
SIDEBAR_MAX_ITEMS = 7

def get_sidebar_categories():
    """Return sidebar categories (Miller's Law: max 7)."""
    return [
        ("Home", "Home.md"),
        ("Architecture", [
            ("Overview", "Architecture-Overview.md"),
            ("Ternary State Space", "Architecture-Ternary-State-Space.md"),
            ("Stabilizer Tableau", "Architecture-Stabilizer-Tableau.md"),
            ("MoE Routing", "Architecture-MoE-Routing.md"),
            ("Forward-Forward", "Architecture-Forward-Forward.md"),
            ("SYCL Acceleration", "Architecture-SYCL-Acceleration.md"),
        ]),
        ("API Reference", [
            ("Core API", "API-Core-Reference.md"),
        ]),
        ("Guides", [
            ("Building", "Guides-Building.md"),
            ("SYCL Setup", "Guides-SYCL-Setup.md"),
            ("Contributing", "Guides-Contributing.md"),
        ]),
        ("Research", [
            ("Cognitive Ergonomics", "Research-Cognitive-Ergonomics.md"),
            ("Clifford Entanglement", "Research-Clifford-Entanglement.md"),
            ("Framework Synthesis", "Research-Framework-Synthesis.md"),
        ]),
        ("Decisions", [
            ("ADR-001: Ternary", "Decisions-ADR-001-Ternary.md"),
        ]),
    ]

def generate_sidebar(categories):
    """Generate _Sidebar.md for GitHub Wiki."""
    lines = []
    for item in categories:
        if isinstance(item[1], str):
            lines.append(f"* [{item[0]}]({item[1]})")
        else:
            lines.append(f"**{item[0]}**")
            for sub_name, sub_file in item[1]:
                lines.append(f"  * [{sub_name}]({sub_file})")
    return "\n".join(lines)

def generate_home():
    """Generate Home.md from root README."""
    readme = REPO_ROOT / "README.md"
    if readme.exists():
        content = readme.read_text(encoding="utf-8")
        # Replace relative links to wiki-style
        content = re.sub(
            r'\[([^\]]+)\]\(docs/([^)]+)\)',
            r'[\1](\2)',
            content
        )
        return content
    return "# q_mini_wasm_v2\n\nWelcome to the wiki."

def convert_doc_to_wiki(source_path, wiki_name):
    """Convert a docs/ markdown file to wiki format."""
    if not source_path.exists():
        return None

    content = source_path.read_text(encoding="utf-8")

    # Fix relative links for wiki
    content = re.sub(
        r'\[([^\]]+)\]\(\.\./([^)]+)\)',
        r'[\1](\2)',
        content
    )
    content = re.sub(
        r'\[([^\]]+)\]\(([^)]+)\.md\)',
        r'[\1](\2)',
        content
    )

    return content

def copy_research_files():
    """Copy research papers to wiki."""
    research_dir = DOCS_DIR / "research"
    wiki_files = {}

    if research_dir.exists():
        mapping = {
            "Cognitive Ergonomics Model Protocol Research.md": "Research-Cognitive-Ergonomics.md",
            "Enhancing Framework with Clifford Entanglement.md": "Research-Clifford-Entanglement.md",
            "QMINIWASM_ Quantum-Classical Framework Synthesis.md": "Research-Framework-Synthesis.md",
        }
        for src_name, wiki_name in mapping.items():
            src = research_dir / src_name
            if src.exists():
                wiki_files[wiki_name] = src.read_text(encoding="utf-8")

    return wiki_files

def main():
    print("Generating wiki from docs/...")

    # Clean output
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    wiki_files = {}

    # Home page
    wiki_files["Home.md"] = generate_home()

    # Architecture docs
    arch_mapping = {
        "overview.md": "Architecture-Overview.md",
        "ternary-state-space.md": "Architecture-Ternary-State-Space.md",
        "stabilizer-tableau.md": "Architecture-Stabilizer-Tableau.md",
        "moe-routing.md": "Architecture-MoE-Routing.md",
        "forward-forward.md": "Architecture-Forward-Forward.md",
        "sycl-acceleration.md": "Architecture-SYCL-Acceleration.md",
    }
    for src_name, wiki_name in arch_mapping.items():
        content = convert_doc_to_wiki(
            DOCS_DIR / "architecture" / src_name, wiki_name
        )
        if content:
            wiki_files[wiki_name] = content

    # API docs
    api_content = convert_doc_to_wiki(
        DOCS_DIR / "api" / "core-reference.md", "API-Core-Reference.md"
    )
    if api_content:
        wiki_files["API-Core-Reference.md"] = api_content

    # Guides
    guide_mapping = {
        "building.md": "Guides-Building.md",
        "sycl-setup.md": "Guides-SYCL-Setup.md",
        "contributing.md": "Guides-Contributing.md",
    }
    for src_name, wiki_name in guide_mapping.items():
        content = convert_doc_to_wiki(
            DOCS_DIR / "guides" / src_name, wiki_name
        )
        if content:
            wiki_files[wiki_name] = content

    # Decisions
    adr_content = convert_doc_to_wiki(
        DOCS_DIR / "decisions" / "adr-001-ternary-over-binary.md",
        "Decisions-ADR-001-Ternary.md"
    )
    if adr_content:
        wiki_files["Decisions-ADR-001-Ternary.md"] = adr_content

    # Research
    wiki_files.update(copy_research_files())

    # Sidebar
    categories = get_sidebar_categories()
    wiki_files["_Sidebar.md"] = generate_sidebar(categories)

    # Write all files
    for name, content in wiki_files.items():
        output = OUTPUT_DIR / name
        output.write_text(content, encoding="utf-8")
        print(f"  Created: {name}")

    print(f"Wiki generated: {len(wiki_files)} pages -> {OUTPUT_DIR}")

if __name__ == "__main__":
    main()