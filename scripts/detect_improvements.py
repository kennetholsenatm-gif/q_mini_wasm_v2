#!/usr/bin/env python3
import json
import argparse

def detect_improvements(rag_analysis_path, mcp_analysis_path, output_path):
    with open(rag_analysis_path) as f:
        rag_analysis = json.load(f)
    
    with open(mcp_analysis_path) as f:
        mcp_analysis = json.load(f)
    
    improvements = []
    improvements.extend(rag_analysis.get('improvements', []))
    improvements.extend(mcp_analysis.get('improvements', []))
    
    results = {
        'timestamp': '2026-04-02T00:00:00Z',
        'total_improvements': len(improvements),
        'improvements': improvements,
        'priority_summary': {
            'high': len([i for i in improvements if i.get('priority') == 'high']),
            'medium': len([i for i in improvements if i.get('priority') == 'medium']),
            'low': len([i for i in improvements if i.get('priority') == 'low'])
        }
    }
    
    with open(output_path, 'w') as f:
        json.dump(results, f, indent=2)
    print('Improvements detected and saved to ' + output_path)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--rag-analysis', required=True)
    parser.add_argument('--mcp-analysis', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    detect_improvements(args.rag_analysis, args.mcp_analysis, args.output)
