#!/usr/bin/env python3
"""
Deep Research Prompt Generator for q_mini_wasm_v2

Generates comprehensive research prompts for a deep research AI agent
based on identified research gaps and core concepts.
"""

import argparse
import json
import os
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Any


def load_gap_report(gap_report_path: str) -> Dict[str, Any]:
    """Load the research gap analysis report."""
    with open(gap_report_path, 'r') as f:
        return json.load(f)


def load_config(config_path: str) -> Dict[str, Any]:
    """Load research alignment configuration."""
    with open(config_path, 'r') as f:
        return json.load(f)


def load_core_concepts(core_concepts_path: str) -> Dict[str, Any]:
    """Load core concepts definition."""
    with open(core_concepts_path, 'r') as f:
        return json.load(f)


def generate_prompt_for_gap(gap: Dict[str, Any], 
                           config: Dict[str, Any],
                           core_concepts: Dict[str, Any],
                           focus: str,
                           depth: str) -> str:
    """Generate a deep research prompt for a specific gap."""
    concept_key = gap.get('concept', 'unknown')
    concept_name = gap.get('concept_name', 'Unknown Concept')
    priority = gap.get('priority', 'medium')
    
    # Get concept details from core concepts
    concept_details = core_concepts.get('core_concepts', {}).get(concept_key, {})
    key_properties = concept_details.get('key_properties', [])
    references = concept_details.get('references', [])
    
    # Get excluded topics from config
    excluded_topics = config.get('prompt_generation', {}).get('excluded_topics', [])
    
    # Generate prompt
    prompt = f"""# Deep Research Prompt: {concept_name}

## Context
This research prompt is generated for the q_mini_wasm_v2 quantum-classical hybrid AI framework.
**Research Gap:** {concept_name} documentation coverage is insufficient.
**Current Coverage:** {gap.get('current_coverage', 0):.1%}
**Priority:** {priority.upper()}

## Research Question
Conduct comprehensive research on {concept_name}:
1. What are the latest advances in {concept_name} for quantum-classical hybrid systems?
2. How can {concept_name} enhance q_mini_wasm_v2's performance?
3. What optimization opportunities exist?

## Key Properties to Investigate
"""
    
    for prop in key_properties[:5]:
        prompt += f"- {prop}\n"
    
    prompt += f"""
## Methodology
1. Literature Review: Survey recent papers and implementations
2. Comparative Analysis: Compare approaches and trade-offs
3. Implementation Assessment: Evaluate feasibility for q_mini_wasm_v2
4. Performance Modeling: Estimate energy efficiency

## Expected Outcomes
1. Technical report on {concept_name}
2. Implementation recommendations
3. Performance benchmarks
4. Integration roadmap

## Alignment Requirements
- Align with ternary state space operations
- Maintain GF(3) arithmetic constraints
- Target energy efficiency <1 pJ/op
- Support extreme-edge deployment

## EXCLUDED TOPICS
"""
    
    for topic in excluded_topics:
        prompt += f"- {topic}\n"
    
    return prompt


def main():
    parser = argparse.ArgumentParser(description='Generate deep research prompts')
    parser.add_argument('--gap-report', required=True, help='Path to gap analysis report')
    parser.add_argument('--config', required=True, help='Research alignment config')
    parser.add_argument('--core-concepts', required=True, help='Core concepts definition')
    parser.add_argument('--focus', default='all', help='Research focus area')
    parser.add_argument('--depth', default='comprehensive', help='Research depth level')
    parser.add_argument('--output-dir', required=True, help='Output directory for prompts')
    
    args = parser.parse_args()
    
    # Load data
    gap_report = load_gap_report(args.gap_report)
    config = load_config(args.config)
    core_concepts = load_core_concepts(args.core_concepts)
    
    # Create output directory
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Get gaps to process
    gaps = gap_report.get('gaps', [])
    
    if not gaps:
        print("No research gaps found. Nothing to generate.")
        return 0
    
    # Filter by focus if specified
    if args.focus != 'all':
        gaps = [g for g in gaps if g.get('concept') == args.focus]
    
    # Generate prompts for each gap
    generated_files = []
    timestamp = datetime.utcnow().strftime('%Y%m%d_%H%M%S')
    
    for i, gap in enumerate(gaps):
        prompt_content = generate_prompt_for_gap(
            gap, config, core_concepts, args.focus, args.depth
        )
        
        # Create filename
        concept_key = gap.get('concept', f'gap_{i}')
        filename = f"deep_research_prompt_{concept_key}_{timestamp}.md"
        filepath = output_dir / filename
        
        # Write prompt file
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(prompt_content)
        
        generated_files.append(str(filepath))
        print(f"Generated prompt: {filename}")
    
    # Create summary file
    summary = {
        'generated_at': datetime.utcnow().isoformat(),
        'focus': args.focus,
        'depth': args.depth,
        'gaps_processed': len(gaps),
        'prompts_generated': len(generated_files),
        'files': generated_files
    }
    
    summary_path = output_dir / f"generation_summary_{timestamp}.json"
    with open(summary_path, 'w', encoding='utf-8') as f:
        json.dump(summary, f, indent=2)
    
    print(f"\nGeneration complete:")
    print(f"  Prompts generated: {len(generated_files)}")
    print(f"  Output directory: {args.output_dir}")
    
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main())