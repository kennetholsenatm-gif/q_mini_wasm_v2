import re
from typing import Dict, Any, List
from mcp.server.fastmcp import FastMCP

mcp = FastMCP("Cognitive Ergonomics")

@mcp.tool()
def lint_toml_ergonomics(toml_content: str) -> Dict[str, Any]:
    """Analyze a TOML configuration against the OmniGraph Cognitive Blueprint."""
    issues = []
    lines = toml_content.split('\n')
    
    global_metadata_found = False
    node_found = False
    
    for i, line in enumerate(lines):
        line_num = i + 1
        stripped = line.strip()
        
        # Skip empty lines and comments
        if not stripped or stripped.startswith('#'):
            continue
            
        # Check global-first ordering
        if stripped.startswith('[omni_metadata]') or stripped.startswith('[global_state]') or stripped.startswith('[global_variables]'):
            global_metadata_found = True
            if node_found:
                issues.append({
                    "line": line_num,
                    "issue": "Global vs Local Ordering",
                    "description": "Global metadata/variables should be placed at the absolute top of the document to prime the predictive engine, before any [[node]] arrays."
                })
                
        if stripped.startswith('[[node]]'):
            node_found = True
            # Check for event boundaries (blank line before [[node]])
            if i > 0 and lines[i-1].strip() and not lines[i-1].strip().startswith('#'):
                issues.append({
                    "line": line_num,
                    "issue": "Topographical Event Boundaries",
                    "description": "Missing blank line before [[node]]. Use double blank lines to flush working memory and create a visual event boundary."
                })
                
        # Check for camelCase or PascalCase keys (ignoring values)
        # Match keys: anything before '='
        if '=' in stripped:
            key_part = stripped.split('=')[0].strip()
            if re.search(r'[a-z][A-Z]', key_part) or re.search(r'^[A-Z][a-z]', key_part):
                issues.append({
                    "line": line_num,
                    "issue": "Saccadic Fluency Identifier",
                    "description": f"Key '{key_part}' violates snake_case requirement. Use snake_case to speed up visual parsing and lower fixation durations."
                })
                
        # Check for deep nesting (indentation)
        if re.match(r'^ {3,}', line) or '\t' in line:
            # We don't flag multi-line arrays but we flag deep physical nesting keys
            if '=' in stripped and not stripped.startswith('depends_on'):
                issues.append({
                    "line": line_num,
                    "issue": "Visual Nesting Overload",
                    "description": "Deep visual nesting detected. Flatten the hierarchy using dotted keys (e.g., config.network.port) to eradicate robust maintenance memory overload."
                })

    return {
        "status": "Analysis Complete",
        "total_issues": len(issues),
        "issues": issues,
        "recommendation": "Review the 'Cognitive Schema Design for OmniGraph' resource for more context."
    }

@mcp.tool()
def lint_doc_ergonomics(doc_content: str) -> Dict[str, Any]:
    """Analyze markdown documentation for cognitive ergonomics violations (Miller's Law, chunking)."""
    issues = []
    paragraphs = re.split(r'\n\s*\n', doc_content)
    
    for i, para in enumerate(paragraphs):
        stripped = para.strip()
        if not stripped:
            continue
            
        # Check for Wall of Text (Miller's Law)
        if len(stripped) > 800 or stripped.count('\n') > 8:
            # exclude code blocks from wall of text
            if not stripped.startswith('```'):
                issues.append({
                    "paragraph_index": i + 1,
                    "issue": "Wall of Text (Miller's Law Violation)",
                    "description": "Paragraph is too long and dense. Overwhelms working memory instantly. Break into shorter chunks, utilize bolding or bullet points."
                })
                
        # Check for Split-Attention (Code block without preceding text context)
        if stripped.startswith('```') and i > 0:
            prev_para = paragraphs[i-1].strip()
            # If the previous paragraph is very long or seems unrelated, might be split attention
            # Heuristic: is the previous text completely separated by too much whitespace?
            pass # Basic static analysis is limited here, prompt review is better

    return {
        "status": "Analysis Complete",
        "total_issues": len(issues),
        "issues": issues
    }

@mcp.resource("file://docs/research/cognitive_ergonomics_wui.md")
def read_wui_research() -> str:
    """Read the Foundational Cognitive Ergonomics Research for WUIs."""
    with open("docs/research/cognitive_ergonomics_wui.md", "r", encoding="utf-8") as f:
        return f.read()

@mcp.resource("file://docs/research/cognitive_schema_design.md")
def read_schema_research() -> str:
    """Read the Neuroscientific Approach to TOML Schema Design."""
    with open("docs/research/cognitive_schema_design.md", "r", encoding="utf-8") as f:
        return f.read()

@mcp.prompt()
def review_wui_design(component_code: str) -> str:
    """Prompt the LLM to review a Web User Interface (WUI) component based on Cognitive Ergonomics."""
    return f"""Please review the following WUI component code/description against the Cognitive Ergonomics principles:

1. **Extraneous Load Minimization**: Does the UI use strict visual hierarchy, distinct geometries, and elevated drop-shadows for critical components? Are secondary components visually suppressed?
2. **Gestalt-Driven Architecture**: Are related items grouped physically (Law of Proximity)? Do items sharing operational roles share identical shapes/colors (Law of Similarity)?
3. **Progressive Disclosure**: Are micro-level details hidden behind interactive expansion panels? Does the interface present a macro-level DAG first?
4. **Hick-Hyman Law**: Does the interface dynamically elevate the top 2-3 most probable remediation paths and hide secondary actions in context menus?
5. **Fitts's Law**: Are interaction hitboxes generous for common actions? Are destructive actions placed further away or require smaller target acquisition?
6. **Graceful Error Recovery**: Does the interface utilize deep contextual error recovery instead of opaque dead-end errors?

Target WUI Code/Description:
```
{component_code}
```

Provide a structured review detailing specific cognitive violations and concrete remediation steps."""

@mcp.prompt()
def review_toml_schema(toml_content: str) -> str:
    """Prompt the LLM to review a TOML schema based on the OmniGraph Cognitive Blueprint."""
    return f"""Please review the following TOML schema against the OmniGraph Cognitive Blueprint axioms:

1. **Prime the Predictive Engine**: Is global metadata/state placed at the absolute top?
2. **Eradicate WM Overflow via Visual Flattening**: Are dotted keys used instead of deep physical nesting/indentation?
3. **Enforce Spatial Contiguity**: Are tightly coupled attributes (like coordinates or scaling pairs) grouped using TOML inline tables?
4. **Deploy Topographical Event Boundaries**: Are distinct functional blocks separated by double blank lines?
5. **Isolate Graph Topology**: Are graph dependencies (depends_on) isolated at the bottom of node blocks using flat relational linking?
6. **Optimize Identifiers**: Are all keys formatted in strict `snake_case`?

Target TOML Schema:
```toml
{toml_content}
```

Provide a structured review detailing specific violations and refactored TOML code snippets."""

if __name__ == '__main__':
    mcp.run()
