#!/usr/bin/env python3
"""
Wiki Generator for q_mini_wasm_v2 - Auto-Discovery Edition

Completely rewrites wiki from docs/ directory with:
- Auto-discovery of all markdown files
- Dynamic categorization by directory
- Embedded Mermaid diagrams
- Cognitive ergonomics enforcement

Usage: python generate_wiki.py
"""

import os
import shutil
import re
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import sys

REPO_ROOT = Path(__file__).parent.parent.parent
DOCS_DIR = REPO_ROOT / "docs"
OUTPUT_DIR = REPO_ROOT / "wiki-output"
DIAGRAMS_DIR = DOCS_DIR / "diagrams"

# Diagram embedding mappings: (diagram_file, target_wiki_page)
DIAGRAM_EMBEDDINGS = [
    ("architecture.md", "Architecture-Overview.md"),
    ("data_flow.md", "Architecture-Overview.md"),
    ("component_interaction.md", "Architecture-Overview.md"),
    ("qgnn_data_flow.md", "Architecture-QGNN.md"),
    ("memory_layout.md", "Architecture-QGNN.md"),
    ("build_pipeline.md", "Guides-Building.md"),
    ("user_journey.md", "Guides-Quick-Start.md"),
]

def kebab_to_title(name: str) -> str:
    """Convert kebab-case-filename.md to Title Case."""
    name = name.replace('.md', '')
    words = re.split(r'[-_]', name)
    result = []
    for word in words:
        word = word.upper() if word.lower() in ['api', 'adr', 'qgnn', 'moe', 'sycl', 'gpu', 'cpu', 'simd', 'wasm'] else word.capitalize()
        result.append(word)
    return ' '.join(result)

def discover_docs() -> Dict[str, List[Tuple[str, str, Path]]]:
    """Auto-discover all markdown files in docs/ directory."""
    categories = {}
    
    for root, dirs, files in os.walk(DOCS_DIR):
        dirs[:] = [d for d in dirs if d not in ['wiki-pipeline', 'research-pipeline']]
        
        for file in files:
            if not file.endswith('.md'):
                continue
                
            source_path = Path(root) / file
            rel_path = source_path.relative_to(DOCS_DIR)
            
            if len(rel_path.parts) == 1:
                category = 'root'
            else:
                category = rel_path.parts[0]
            
            if file == 'README.md' and category == 'root':
                continue
            
            display_name = kebab_to_title(file)
            wiki_filename = f"{kebab_to_title(category)}-{kebab_to_title(file)}.md" if category != 'root' else f"{display_name}.md"
            wiki_filename = wiki_filename.replace('Root-', '')
            
            if category not in categories:
                categories[category] = []
            
            categories[category].append((display_name, wiki_filename, source_path))
    
    return categories

def extract_mermaid_diagrams(diagram_path: Path) -> List[str]:
    """Extract all mermaid diagrams from a file."""
    if not diagram_path.exists():
        return []
    
    content = diagram_path.read_text(encoding='utf-8')
    diagrams = []
    pattern = r'```mermaid\n(.*?)```'
    matches = re.findall(pattern, content, re.DOTALL)
    
    for match in matches:
        diagrams.append(f"```mermaid\n{match}```")
    
    return diagrams

def convert_links(content: str, current_category: str) -> str:
    """Convert relative links to wiki-style links."""
    def replace_link(match):
        text = match.group(1)
        link_path = match.group(2)
        
        if link_path.startswith('http') or link_path.startswith('#'):
            return match.group(0)
        
        if link_path.endswith('.md'):
            parts = link_path.replace('../', '').replace('./', '').split('/')
            if len(parts) >= 2:
                category = parts[-2] if len(parts) > 1 else current_category
                filename = parts[-1]
                wiki_name = f"{kebab_to_title(category)}-{kebab_to_title(filename)}.md"
                return f'[{text}]({wiki_name})'
            else:
                return f'[{text}]({kebab_to_title(link_path)})'
        
        return match.group(0)
    
    content = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', replace_link, content)
    return content

def process_content(content: str, source_path: Path, categories: Dict) -> str:
    """Process content for wiki: fix links."""
    rel_path = source_path.relative_to(DOCS_DIR)
    category = str(rel_path.parts[0]) if len(rel_path.parts) > 1 else 'root'
    content = convert_links(content, category)
    return content

def get_diagram_for_page(wiki_filename: str, diagrams_content: Dict[str, List[str]]) -> Optional[str]:
    """Get diagrams to embed in a specific wiki page."""
    result = []
    
    for diagram_file, target_page in DIAGRAM_EMBEDDINGS:
        if target_page == wiki_filename and diagram_file in diagrams_content:
            diagrams = diagrams_content[diagram_file]
            if diagrams:
                result.append("\n## Architecture Diagram\n")
                result.extend(diagrams[:1])
    
    return '\n'.join(result) if result else None

def generate_wiki_pages(categories: Dict) -> Dict[str, str]:
    """Generate all wiki pages from discovered docs."""
    wiki_files = {}
    
    diagrams_content = {}
    if DIAGRAMS_DIR.exists():
        for diag_file in DIAGRAMS_DIR.glob('*.md'):
            diagrams_content[diag_file.name] = extract_mermaid_diagrams(diag_file)
    
    for category, files in categories.items():
        for display_name, wiki_filename, source_path in files:
            content = source_path.read_text(encoding='utf-8')
            content = process_content(content, source_path, categories)
            
            diagram = get_diagram_for_page(wiki_filename, diagrams_content)
            if diagram:
                lines = content.split('\n')
                new_lines = []
                inserted = False
                for i, line in enumerate(lines):
                    new_lines.append(line)
                    if not inserted and line.startswith('# ') and i < len(lines) - 1:
                        new_lines.append('')
                        new_lines.append(diagram)
                        inserted = True
                content = '\n'.join(new_lines)
            
            wiki_files[wiki_filename] = content
    
    return wiki_files

def generate_home_page() -> str:
    """Generate Home.md from README with proper formatting."""
    readme = REPO_ROOT / "README.md"
    if not readme.exists():
        return "# q_mini_wasm_v2\n\nWelcome to the wiki."
    
    content = readme.read_text(encoding='utf-8')
    content = re.sub(r'\[!\[.*?\]\(.*?\)\]\(.*?\)\n?', '', content)
    content = re.sub(r'\[!\[.*?\]\(.*?\)\]\n?', '', content)
    content = re.sub(r'\[([^\]]+)\]\(docs/([^)]+)\)', r'[\1](\2)', content)
    
    intro = """# q_mini_wasm_v2 Wiki

> **Complete documentation for the quantum-inspired, ternary AI inference engine**

## Navigation

- **Getting Started**: Quick start, building, and setup guides
- **Architecture**: System design, QGNN, MoE routing, and core concepts
- **API Reference**: Core API documentation
- **Research**: Papers on quantum computing, cognitive ergonomics, and Betti numbers
- **Decisions**: Architecture Decision Records (ADRs)

---

"""
    
    return intro + content

def generate_sidebar(categories: Dict) -> str:
    """Generate _Sidebar.md with organized structure (max 7 items)."""
    lines = []
    lines.append("# Wiki Navigation")
    lines.append("")
    lines.append("* [Home](Home.md)")
    lines.append("")
    
    lines.append("**Getting Started**")
    if 'guides' in categories:
        guides = sorted(categories['guides'], key=lambda x: (
            0 if 'quick' in x[0].lower() else
            1 if 'build' in x[0].lower() else
            2 if 'sycl' in x[0].lower() else
            3,
            x[0]
        ))
        for display_name, wiki_filename, _ in guides:
            lines.append(f"  * [{display_name}]({wiki_filename})")
    lines.append("")
    
    lines.append("**Architecture**")
    if 'architecture' in categories:
        arch = sorted(categories['architecture'], key=lambda x: (
            0 if 'overview' in x[0].lower() else 1,
            x[0]
        ))
        for display_name, wiki_filename, _ in arch:
            lines.append(f"  * [{display_name}]({wiki_filename})")
    lines.append("")
    
    lines.append("**API Reference**")
    if 'api' in categories:
        for display_name, wiki_filename, _ in sorted(categories['api']):
            lines.append(f"  * [{display_name}]({wiki_filename})")
    lines.append("")
    
    lines.append("**Advanced Guides**")
    advanced_items = []
    if 'root' in categories:
        for item in categories['root']:
            if 'BETTI' not in item[0].upper():
                advanced_items.append(item)
    for display_name, wiki_filename, _ in sorted(advanced_items):
        lines.append(f"  * [{display_name}]({wiki_filename})")
    lines.append("")
    
    lines.append("**Research**")
    if 'research' in categories:
        for display_name, wiki_filename, _ in sorted(categories['research']):
            lines.append(f"  * [{display_name}]({wiki_filename})")
    if 'root' in categories:
        for display_name, wiki_filename, _ in categories['root']:
            if 'BETTI' in display_name.upper():
                lines.append(f"  * [{display_name}]({wiki_filename})")
    lines.append("")
    
    lines.append("**Decisions**")
    if 'decisions' in categories:
        for display_name, wiki_filename, _ in sorted(categories['decisions']):
            lines.append(f"  * [{display_name}]({wiki_filename})")
    lines.append("")
    
    return '\n'.join(lines)

def ensure_cognitive_ergonomics(content: str) -> str:
    """Apply cognitive ergonomics principles to content."""
    lines = content.split('\n')
    new_lines = []
    word_count = 0
    last_header_idx = 0
    in_code_block = False
    code_block_lines = []
    is_mermaid = False
    
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        
        if stripped.startswith('```'):
            if in_code_block:
                # End of code block
                if is_mermaid:
                    # Don't truncate mermaid diagrams
                    new_lines.extend(code_block_lines)
                    new_lines.append(line)
                elif len(code_block_lines) > 15:
                    # Truncate long code blocks
                    code_block_lines = code_block_lines[:12] + ['  // ... (truncated)', '  // See source for complete code', '```']
                    new_lines.extend(code_block_lines)
                else:
                    new_lines.extend(code_block_lines)
                    new_lines.append(line)
                in_code_block = False
                is_mermaid = False
                code_block_lines = []
            else:
                # Start of code block
                in_code_block = True
                is_mermaid = stripped.startswith('```mermaid')
                code_block_lines.append(line)
            i += 1
            continue
        
        if in_code_block:
            code_block_lines.append(line)
            i += 1
            continue
        
        if stripped.startswith('#'):
            word_count = 0
            last_header_idx = len(new_lines)
            new_lines.append(line)
            i += 1
            continue
        
        if stripped:
            words = len(stripped.split())
            word_count += words
            
            if word_count > 300 and last_header_idx > 0:
                new_lines.append('')
                new_lines.append('### Continue Reading')
                new_lines.append('')
                word_count = 0
                last_header_idx = len(new_lines)
        
        if len(stripped) > 75 and not stripped.startswith('|') and not stripped.startswith('```'):
            words = stripped.split()
            current_line = []
            current_len = 0
            wrapped_lines = []
            
            for word in words:
                if current_len + len(word) + 1 > 75:
                    wrapped_lines.append(' '.join(current_line))
                    current_line = [word]
                    current_len = len(word)
                else:
                    current_line.append(word)
                    current_len += len(word) + 1
            
            if current_line:
                wrapped_lines.append(' '.join(current_line))
            
            new_lines.extend(wrapped_lines)
        else:
            new_lines.append(line)
        
        i += 1
    
    return '\n'.join(new_lines)

def main():
    print("=" * 60)
    print("q_mini_wasm_v2 Wiki Generator - Auto-Discovery Edition")
    print("=" * 60)
    
    if OUTPUT_DIR.exists():
        print(f"Cleaning {OUTPUT_DIR}...")
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    print(f"Created: {OUTPUT_DIR}")
    
    print("\n[1/4] Discovering documentation files...")
    categories = discover_docs()
    total_files = sum(len(files) for files in categories.values())
    print(f"  Found {total_files} markdown files in {len(categories)} categories")
    for cat, files in sorted(categories.items()):
        print(f"    - {cat}: {len(files)} files")
    
    print("\n[2/4] Generating wiki pages...")
    wiki_files = generate_wiki_pages(categories)
    
    print("\n[3/4] Generating Home.md...")
    wiki_files['Home.md'] = generate_home_page()
    
    print("\n[4/4] Generating _Sidebar.md...")
    wiki_files['_Sidebar.md'] = generate_sidebar(categories)
    
    print("\n[5/4] Applying cognitive ergonomics...")
    for filename in wiki_files:
        wiki_files[filename] = ensure_cognitive_ergonomics(wiki_files[filename])
    
    print("\n" + "=" * 60)
    print("Writing wiki files...")
    print("=" * 60)
    for name, content in sorted(wiki_files.items()):
        output = OUTPUT_DIR / name
        output.write_text(content, encoding="utf-8")
        size_kb = len(content) / 1024
        print(f"  {name} ({size_kb:.1f} KB)")
    
    print(f"\n{'=' * 60}")
    print(f"✅ Wiki generation complete!")
    print(f"   Total pages: {len(wiki_files)}")
    print(f"   Output: {OUTPUT_DIR}")
    print(f"{'=' * 60}")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
