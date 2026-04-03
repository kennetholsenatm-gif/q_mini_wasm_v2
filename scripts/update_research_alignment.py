#!/usr/bin/env python3
"""
Update Research Alignment Configuration

Updates the research alignment configuration based on gap analysis results.
"""

import argparse
import json
from pathlib import Path
from datetime import datetime


def load_gap_report(gap_report_path: str) -> dict:
    """Load gap analysis report."""
    with open(gap_report_path, 'r') as f:
        return json.load(f)


def load_config(config_path: str) -> dict:
    """Load current research alignment config."""
    with open(config_path, 'r') as f:
        return json.load(f)


def update_alignment(config: dict, gap_report: dict) -> dict:
    """Update alignment configuration based on gap analysis."""
    updated_config = config.copy()
    
    # Update core framework areas with gap information
    core_areas = updated_config.get('core_framework_areas', {})
    gaps = gap_report.get('gaps', [])
    coverage = gap_report.get('concepts_coverage', {})
    
    for area_key, area_data in core_areas.items():
        if area_key in coverage:
            area_data['last_coverage_score'] = coverage[area_key].get('coverage_score', 0)
            area_data['last_analyzed'] = datetime.utcnow().isoformat()
    
    # Add gap information
    updated_config['identified_gaps'] = gaps
    updated_config['last_gap_analysis'] = datetime.utcnow().isoformat()
    
    # Update pipeline status
    updated_config['pipeline_status'] = {
        'last_run': datetime.utcnow().isoformat(),
        'gaps_detected': len(gaps),
        'overall_coverage': gap_report.get('overall_coverage', 0)
    }
    
    return updated_config


def main():
    parser = argparse.ArgumentParser(description='Update research alignment')
    parser.add_argument('--gap-report', required=True, help='Path to gap report')
    parser.add_argument('--config', required=True, help='Current config path')
    parser.add_argument('--output', required=True, help='Output path for updated config')
    
    args = parser.parse_args()
    
    # Load data
    gap_report = load_gap_report(args.gap_report)
    config = load_config(args.config)
    
    # Update alignment
    updated_config = update_alignment(config, gap_report)
    
    # Write output
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(updated_config, f, indent=2)
    
    print(f"Research alignment updated successfully")
    print(f"  Gaps detected: {len(gap_report.get('gaps', []))}")
    print(f"  Output: {args.output}")
    
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main())