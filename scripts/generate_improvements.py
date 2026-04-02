#!/usr/bin/env python3
import json
import argparse
import os

def generate_improvements(input_path, output_dir):
    with open(input_path) as f:
        improvements_data = json.load(f)
    
    os.makedirs(output_dir, exist_ok=True)
    os.makedirs(os.path.join(output_dir, 'rag'), exist_ok=True)
    os.makedirs(os.path.join(output_dir, 'mcp'), exist_ok=True)
    
    for improvement in improvements_data.get('improvements', []):
        imp_type = improvement.get('type', 'general')
        if imp_type in ['indexing', 'query']:
            output_file = os.path.join(output_dir, 'rag', imp_type + '_improvement.json')
        else:
            output_file = os.path.join(output_dir, 'mcp', imp_type + '_improvement.json')
        
        with open(output_file, 'w') as f:
            json.dump(improvement, f, indent=2)
    
    print('Improvements generated in ' + output_dir)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', required=True)
    parser.add_argument('--output-dir', required=True)
    args = parser.parse_args()
    generate_improvements(args.input, args.output_dir)
