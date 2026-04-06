#!/usr/bin/env python3
"""
Glossary Term Extractor for q_mini_wasm_v2

Scans source code, documentation, and configuration files to extract
technical terms for the wiki glossary with definitions.

Usage: python extract_glossary.py
Output: wui/data/glossary.json
"""

import os
import re
import json
from pathlib import Path
from collections import defaultdict
from typing import Dict, List, Set, Tuple

REPO_ROOT = Path(__file__).parent.parent
CORE_DIR = REPO_ROOT / "q_mini_wasm_v2" / "core"
DOCS_DIR = REPO_ROOT / "docs"
CONFIG_DIR = REPO_ROOT / "config"
OUTPUT_DIR = REPO_ROOT / "wui" / "data"

# Technical term patterns to extract
PATTERNS = {
    "class_struct": r"(?:class|struct)\s+(\w+)",
    "enum": r"enum\s+(?:class\s+)?(\w+)",
    "typedef_using": r"(?:typedef|using)\s+(\w+)",
    "function": r"(?:void|int|bool|size_t|auto|std::\w+|\w+::\w+)\s+(\w+)\s*\(",
    "constant": r"(?:constexpr|const)\s+(?:\w+)\s+(\w+)\s*=",
    "acronym": r"\b([A-Z]{2,})\b",
    "camel_case": r"\b([A-Z][a-z]+(?:[A-Z][a-z]+)+)\b",
    "snake_case_tech": r"\b([a-z]+_[a-z_]+(?:_[a-z]+)*)\b",
}

# Known technical terms that should always be included
KNOWN_TERMS = {
    "GF(3)": {
        "term": "GF(3)",
        "full_form": "Galois Field of order 3",
        "definition": "A finite field with three elements {0, 1, 2} used for ternary arithmetic. All operations are performed modulo 3.",
        "category": "mathematics",
        "source": "core/ternary/",
    },
    "QGNN": {
        "term": "QGNN",
        "full_form": "Quantum Graph Neural Network",
        "definition": "A graph-native neural network architecture using quantum-inspired message passing over ternary-valued edges.",
        "category": "architecture",
        "source": "core/qgnn/",
    },
    "MoE": {
        "term": "MoE",
        "full_form": "Mixture of Experts",
        "definition": "A routing mechanism that directs inputs to specialized expert networks using tropical (max-plus) algebra.",
        "category": "architecture",
        "source": "core/routing/",
    },
    "SYCL": {
        "term": "SYCL",
        "full_form": "Standard C++ for Heterogeneous Computing",
        "definition": "An open standard C++ abstraction layer for programming heterogeneous devices including GPUs, FPGAs, and other accelerators.",
        "category": "hardware",
        "source": "core/qgnn/",
    },
    "USM": {
        "term": "USM",
        "full_form": "Unified Shared Memory",
        "definition": "A memory model where host and device share a single address space, enabling zero-copy data transfers.",
        "category": "hardware",
        "source": "core/memory/",
    },
    "WASM": {
        "term": "WASM",
        "full_form": "WebAssembly",
        "definition": "A binary instruction format for a stack-based virtual machine, enabling high-performance applications on the web.",
        "category": "technology",
        "source": "dll/wasm_bridge/",
    },
    "Trit": {
        "term": "Trit",
        "full_form": "Ternary Digit",
        "definition": "The basic unit of information in ternary computing, with three possible states: -1, 0, or +1.",
        "category": "mathematics",
        "source": "core/ternary/trit.hpp",
    },
    "Tryte": {
        "term": "Tryte",
        "full_form": "Ternary Byte",
        "definition": "A grouping of trits analogous to a byte. Typically 5 or 6 trits, balancing ternary efficiency with binary compatibility.",
        "category": "mathematics",
        "source": "core/ternary/",
    },
    "Clifford": {
        "term": "Clifford",
        "full_form": "Clifford Algebra",
        "definition": "Algebraic framework for quantum operations. Clifford gates (H, S, CNOT) can be efficiently simulated classically.",
        "category": "mathematics",
        "source": "core/stabilizer/",
    },
    "Stabilizer": {
        "term": "Stabilizer",
        "full_form": "Stabilizer Formalism",
        "definition": "A method for describing quantum states using operators that leave the state unchanged. Enables O(n²) simulation.",
        "category": "mathematics",
        "source": "core/stabilizer/",
    },
    "Tableau": {
        "term": "Tableau",
        "full_form": "Stabilizer Tableau",
        "definition": "A matrix representation of stabilizer states tracking X and Z Pauli operators for efficient quantum simulation.",
        "category": "mathematics",
        "source": "core/stabilizer/",
    },
    "Qutrit": {
        "term": "Qutrit",
        "full_form": "Quantum Trit",
        "definition": "A three-level quantum system, the ternary equivalent of a qubit. Can exist in superposition of |0⟩, |1⟩, |2⟩ states.",
        "category": "physics",
        "source": "core/stabilizer/",
    },
    "CIM": {
        "term": "CIM",
        "full_form": "Compute-in-Memory",
        "definition": "Architecture that performs computation directly in memory arrays, reducing data movement and energy consumption.",
        "category": "hardware",
        "source": "core/flash_cim/",
    },
    "MLC": {
        "term": "MLC",
        "full_form": "Multi-Level Cell",
        "definition": "Flash memory technology storing multiple bits per cell. Maps naturally to ternary values with error detection.",
        "category": "hardware",
        "source": "core/flash_cim/",
    },
    "Tropical": {
        "term": "Tropical",
        "full_form": "Tropical Algebra",
        "definition": "Mathematics using max-plus semiring (addition becomes max, multiplication becomes plus). Used in MoE routing.",
        "category": "mathematics",
        "source": "core/routing/",
    },
    "Symplectic": {
        "term": "Symplectic",
        "full_form": "Symplectic Geometry",
        "definition": "Geometric structure preserving area in phase space. Used to measure quantum state overlap and entanglement.",
        "category": "mathematics",
        "source": "core/qgnn/",
    },
    "Entanglement": {
        "term": "Entanglement",
        "full_form": "Quantum Entanglement",
        "definition": "Quantum correlation between particles where measurement of one instantaneously affects the other, regardless of distance.",
        "category": "physics",
        "source": "core/qgnn/",
    },
    "Betti": {
        "term": "Betti",
        "full_form": "Betti Numbers",
        "definition": "Topological invariants counting connected components, holes, and voids in a space. Used for quantum error correction.",
        "category": "mathematics",
        "source": "docs/QUANTUM_BETTI_NUMBERS.md",
    },
    "ForwardForward": {
        "term": "Forward-Forward",
        "full_form": "Forward-Forward Learning",
        "definition": "A teacherless learning algorithm using two forward passes with different data to update weights, eliminating backpropagation.",
        "category": "machine_learning",
        "source": "core/learning/",
    },
    "Hebbian": {
        "term": "Hebbian",
        "full_form": "Hebbian Learning",
        "definition": "Learning rule where synaptic strength increases when pre- and post-synaptic neurons fire together. 'Neurons that fire together, wire together.'",
        "category": "neuroscience",
        "source": "core/learning/",
    },
    "Arena": {
        "term": "Arena",
        "full_form": "Memory Arena",
        "definition": "A pre-allocated memory region for fast, deterministic allocation. Supports zero-copy access across WASM, C++, and SYCL.",
        "category": "architecture",
        "source": "core/memory/arena.hpp",
    },
    "GottesmanKnill": {
        "term": "Gottesman-Knill",
        "full_form": "Gottesman-Knill Theorem",
        "definition": "States that stabilizer circuits (Clifford gates) can be efficiently simulated classically in O(n²) time.",
        "category": "physics",
        "source": "docs/architecture/",
    },
    "ECC": {
        "term": "ECC",
        "full_form": "Error Correction Code",
        "definition": "Techniques to detect and correct errors in data transmission or storage. Steane code used for quantum error correction.",
        "category": "reliability",
        "source": "core/flash_cim/",
    },
    "MCP": {
        "term": "MCP",
        "full_form": "Model Context Protocol",
        "definition": "Protocol for AI assistants to interact with external tools and services via standardized interfaces.",
        "category": "integration",
        "source": "core/agents/",
    },
    "RAG": {
        "term": "RAG",
        "full_form": "Retrieval-Augmented Generation",
        "definition": "AI technique combining information retrieval with text generation to improve accuracy and grounding.",
        "category": "ai",
        "source": "core/agents/",
    },
    "DLL": {
        "term": "DLL",
        "full_form": "Dynamic Link Library",
        "definition": "Microsoft's shared library format allowing code sharing between processes at runtime.",
        "category": "technology",
        "source": "dll/",
    },
}

def extract_from_cpp_headers(directory: Path) -> Dict[str, dict]:
    """Extract class names, enums, and significant functions from C++ headers."""
    terms = {}
    
    if not directory.exists():
        return terms
    
    for hpp_file in directory.rglob("*.hpp"):
        content = hpp_file.read_text(encoding="utf-8")
        rel_path = hpp_file.relative_to(REPO_ROOT)
        
        # Extract class names
        for match in re.finditer(PATTERNS["class_struct"], content):
            name = match.group(1)
            if len(name) > 3 and name not in ["class", "struct", "public", "private"]:
                if name not in terms:
                    terms[name] = {
                        "term": name,
                        "type": "class",
                        "source": str(rel_path),
                        "category": categorize_term(name),
                    }
        
        # Extract enums
        for match in re.finditer(PATTERNS["enum"], content):
            name = match.group(1)
            if name not in terms:
                terms[name] = {
                    "term": name,
                    "type": "enum",
                    "source": str(rel_path),
                    "category": categorize_term(name),
                }
        
        # Extract significant constants
        for match in re.finditer(PATTERNS["constant"], content):
            name = match.group(1)
            if len(name) > 5 and name.isupper():
                if name not in terms:
                    terms[name] = {
                        "term": name,
                        "type": "constant",
                        "source": str(rel_path),
                        "category": categorize_term(name),
                    }
    
    return terms

def extract_from_toml(directory: Path) -> Dict[str, dict]:
    """Extract configuration keys from TOML files."""
    terms = {}
    
    if not directory.exists():
        return terms
    
    for toml_file in directory.glob("*.toml"):
        content = toml_file.read_text(encoding="utf-8")
        rel_path = toml_file.relative_to(REPO_ROOT)
        
        # Extract section headers
        for match in re.finditer(r"^\[([^\]]+)\]", content, re.MULTILINE):
            section = match.group(1)
            parts = section.split(".")
            
            # Add each part as a term
            for part in parts:
                if part not in terms and len(part) > 2:
                    terms[part] = {
                        "term": part,
                        "type": "config",
                        "source": str(rel_path),
                        "category": "configuration",
                        "context": section,
                    }
        
        # Extract key-value pairs
        for match in re.finditer(r"^(\w+)\s*=(.+)$", content, re.MULTILINE):
            key = match.group(1)
            value = match.group(2).strip()
            
            if key not in terms and len(key) > 2:
                terms[key] = {
                    "term": key,
                    "type": "config",
                    "source": str(rel_path),
                    "category": "configuration",
                    "example_value": value[:50],
                }
    
    return terms

def extract_from_docs(directory: Path) -> Dict[str, dict]:
    """Extract technical terms from documentation."""
    terms = {}
    
    if not directory.exists():
        return terms
    
    for md_file in directory.rglob("*.md"):
        content = md_file.read_text(encoding="utf-8")
        rel_path = md_file.relative_to(REPO_ROOT)
        
        # Extract acronyms (2+ uppercase letters)
        for match in re.finditer(PATTERNS["acronym"], content):
            acronym = match.group(1)
            if acronym not in terms and len(acronym) <= 5:
                terms[acronym] = {
                    "term": acronym,
                    "type": "acronym",
                    "source": str(rel_path),
                    "category": "abbreviation",
                }
        
        # Extract CamelCase terms
        for match in re.finditer(PATTERNS["camel_case"], content):
            term = match.group(1)
            if term not in terms:
                terms[term] = {
                    "term": term,
                    "type": "concept",
                    "source": str(rel_path),
                    "category": categorize_term(term),
                }
    
    return terms

def categorize_term(term: str) -> str:
    """Categorize a term based on its name and context."""
    term_lower = term.lower()
    
    categories = {
        "mathematics": ["matrix", "vector", "tensor", "algebra", "geometry", "symplectic", "galois", "field", "modulo", "linear"],
        "physics": ["quantum", "qubit", "qutrit", "entanglement", "superposition", "coherence", "state"],
        "hardware": ["memory", "gpu", "fpga", "flash", "cache", "device", "kernel", "simd"],
        "architecture": ["router", "pipeline", "queue", "graph", "node", "edge", "layer", "expert"],
        "machine_learning": ["learning", "training", "inference", "model", "neural", "network", "attention"],
        "software": ["runtime", "compiler", "library", "api", "interface", "buffer", "allocator"],
    }
    
    for category, keywords in categories.items():
        if any(kw in term_lower for kw in keywords):
            return category
    
    return "general"

def enrich_definitions(terms: Dict[str, dict]) -> Dict[str, dict]:
    """Add definitions to terms based on their context and type."""
    enriched = {}
    
    # Start with known terms that have full definitions
    for term, data in KNOWN_TERMS.items():
        enriched[term] = data.copy()
    
    # Add extracted terms with generated definitions
    for term, data in terms.items():
        if term in enriched:
            # Merge additional context
            enriched[term].update(data)
        else:
            # Generate basic definition
            data["definition"] = generate_definition(term, data.get("type", "general"))
            data["related_terms"] = find_related_terms(term, list(terms.keys()))
            enriched[term] = data
    
    return enriched

def generate_definition(term: str, term_type: str) -> str:
    """Generate a basic definition based on term type and name."""
    if term_type == "class":
        return f"A C++ class representing the {camel_to_words(term)} component in the system architecture."
    elif term_type == "enum":
        return f"An enumeration defining valid states or options for {camel_to_words(term)}."
    elif term_type == "constant":
        return f"A compile-time constant defining {snake_to_words(term)}."
    elif term_type == "config":
        return f"Configuration parameter controlling {snake_to_words(term)}."
    elif term_type == "acronym":
        return f"Acronym for {term}. See full definition in related terms."
    else:
        return f"Technical term related to {camel_to_words(term)}."

def camel_to_words(camel: str) -> str:
    """Convert CamelCase to space-separated words."""
    words = re.sub(r'([a-z])([A-Z])', r'\1 \2', camel)
    return words.lower()

def snake_to_words(snake: str) -> str:
    """Convert snake_case to space-separated words."""
    return snake.replace("_", " ").lower()

def find_related_terms(term: str, all_terms: List[str]) -> List[str]:
    """Find terms related to the given term based on shared substrings."""
    related = []
    term_lower = term.lower()
    
    for other in all_terms:
        if other == term:
            continue
        other_lower = other.lower()
        
        # Check for shared substrings (3+ chars)
        for i in range(len(term_lower) - 2):
            substring = term_lower[i:i+3]
            if substring in other_lower:
                related.append(other)
                break
    
    return related[:5]  # Limit to 5 related terms

def save_glossary_json(glossary: Dict[str, dict], output_path: Path):
    """Save glossary to JSON file."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    # Sort by term name
    sorted_glossary = dict(sorted(glossary.items(), key=lambda x: x[0].lower()))
    
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(sorted_glossary, f, indent=2, ensure_ascii=False)
    
    print(f"[OK] Saved glossary.json: {len(sorted_glossary)} terms to {output_path}")

def generate_glossary_md(glossary: Dict[str, dict], output_path: Path):
    """Generate Glossary.md wiki page."""
    lines = [
        "# Glossary",
        "",
        "Complete reference of technical terms used throughout q_mini_wasm_v2.",
        "",
        "## Navigation",
        "",
        "- [By Category](#by-category)",
        "- [Alphabetical Index](#alphabetical-index)",
        "",
    ]
    
    # Group by category
    by_category = defaultdict(list)
    for term, data in sorted(glossary.items(), key=lambda x: x[0].lower()):
        category = data.get("category", "general")
        by_category[category].append((term, data))
    
    # Category view
    lines.append("## By Category")
    lines.append("")
    
    for category in sorted(by_category.keys()):
        lines.append(f"### {category.replace('_', ' ').title()}")
        lines.append("")
        
        for term, data in by_category[category]:
            full = data.get("full_form", "")
            definition = data.get("definition", "")
            
            lines.append(f"**{term}**")
            if full:
                lines.append(f"*{full}*")
            lines.append(f"{definition}")
            
            if "related_terms" in data and data["related_terms"]:
                related = ", ".join(data["related_terms"][:3])
                lines.append(f"See also: {related}")
            
            lines.append("")
    
    # Alphabetical index
    lines.append("## Alphabetical Index")
    lines.append("")
    
    for term, data in sorted(glossary.items(), key=lambda x: x[0].lower()):
        category = data.get("category", "general")
        lines.append(f"- **{term}** ({category})")
    
    lines.append("")
    
    # Write file
    output_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"[OK] Generated Glossary.md: {output_path}")

def main():
    print("=" * 60)
    print("q_mini_wasm_v2 Glossary Extractor")
    print("=" * 60)
    
    # Extract from all sources
    print("\n[1/4] Scanning C++ headers...")
    cpp_terms = extract_from_cpp_headers(CORE_DIR)
    print(f"  Found {len(cpp_terms)} terms from C++ headers")
    
    print("\n[2/4] Scanning TOML configurations...")
    toml_terms = extract_from_toml(CONFIG_DIR)
    print(f"  Found {len(toml_terms)} terms from TOML configs")
    
    print("\n[3/4] Scanning documentation...")
    doc_terms = extract_from_docs(DOCS_DIR)
    print(f"  Found {len(doc_terms)} terms from documentation")
    
    # Merge all terms
    print("\n[4/4] Enriching definitions...")
    all_terms = {**cpp_terms, **toml_terms, **doc_terms}
    glossary = enrich_definitions(all_terms)
    print(f"  Total unique terms: {len(glossary)}")
    
    # Save outputs
    print("\n" + "=" * 60)
    print("Saving outputs...")
    print("=" * 60)
    
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    
    # Save JSON for WUI
    save_glossary_json(glossary, OUTPUT_DIR / "glossary.json")
    
    # Generate wiki page
    generate_glossary_md(glossary, REPO_ROOT / "wiki-output" / "Glossary.md")
    
    print(f"\n{'=' * 60}")
    print("[DONE] Glossary extraction complete!")
    print(f"   Terms: {len(glossary)}")
    print(f"   JSON: wui/data/glossary.json")
    print(f"   Wiki: wiki-output/Glossary.md")
    print(f"{'=' * 60}")
    
    return 0

if __name__ == "__main__":
    import sys
    sys.exit(main())
