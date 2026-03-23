#!/usr/bin/env python3
"""
Environment Variables Documentation Generator

This script reads the .env.schema file and generates a markdown documentation
file for MkDocs. It extracts variable names, descriptions, types, and default
values to create a comprehensive environment variables reference.

Usage: python scripts/generate_env_docs.py
"""

import re
import os
from pathlib import Path
from typing import Dict, List, Optional

class EnvVar:
    """Represents an environment variable with its metadata."""
    
    def __init__(self, name: str):
        self.name = name
        self.description = ""
        self.var_type = "string"
        self.default = ""
        self.sensitive = False
        self.required = True

def parse_env_schema(schema_file: str) -> List[EnvVar]:
    """
    Parse the .env.schema file and extract environment variable information.
    
    Args:
        schema_file: Path to the .env.schema file
        
    Returns:
        List of EnvVar objects with parsed information
    """
    env_vars = []
    current_var = None
    current_description = []
    
    with open(schema_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    for line in lines:
        line = line.strip()
        
        # Skip empty lines and comments that aren't part of Varlock spec
        if not line or line.startswith('#') and not line.startswith('# @'):
            continue
            
        # Check for Varlock annotations
        if line.startswith('@env-spec'):
            if current_var:
                # Save previous variable
                current_var.description = ' '.join(current_description).strip()
                env_vars.append(current_var)
            
            # Start new variable (we'll get the name from the next line)
            current_var = None
            current_description = []
            
        elif line.startswith('@description'):
            if current_var:
                desc = line.replace('@description', '').strip()
                current_description.append(desc)
            else:
                # Store description for next variable
                current_description.append(line.replace('@description', '').strip())
                
        elif line.startswith('@type'):
            if current_var:
                current_var.var_type = line.replace('@type', '').strip()
                
        elif line.startswith('@default'):
            if current_var:
                current_var.default = line.replace('@default', '').strip()
                
        elif line.startswith('@sensitive'):
            if current_var:
                sensitive_value = line.replace('@sensitive', '').strip().lower()
                current_var.sensitive = sensitive_value == 'true'
                
        elif '=' in line and not line.startswith('@'):
            # This should be the variable definition
            name = line.split('=')[0].strip()
            if current_var is None:
                current_var = EnvVar(name)
            else:
                current_var.name = name
                
            # Set description from accumulated lines
            current_var.description = ' '.join(current_description).strip()
            
            # Determine if required (has no default)
            if current_var.default and current_var.default != 'None':
                current_var.required = False
                
            # Add to list and reset
            env_vars.append(current_var)
            current_var = None
            current_description = []
    
    return env_vars

def generate_markdown_table(env_vars: List[EnvVar]) -> str:
    """
    Generate a markdown table from the environment variables.
    
    Args:
        env_vars: List of EnvVar objects
        
    Returns:
        String containing the markdown table
    """
    if not env_vars:
        return "# Environment Variables\n\nNo environment variables found.\n"
    
    # Sort by category (based on common prefixes)
    categories = {
        'IBM Quantum': [],
        'Qiskit': [],
        'Quantum Simulation': [],
        'WASM': [],
        'Intel ARC/GPU': [],
        'Cloud': [],
        'Database': [],
        'Monitoring': [],
        'Docker': [],
        'Development': [],
        'Security': [],
        'Performance': [],
        'Intel Cloud': [],
        'Intel Quantum': [],
        'Other': []
    }
    
    for var in env_vars:
        name = var.name.upper()
        if 'IBM_QUANTUM' in name:
            categories['IBM Quantum'].append(var)
        elif 'QISKIT' in name:
            categories['Qiskit'].append(var)
        elif 'QUANTUM' in name and 'IBM' not in name:
            categories['Quantum Simulation'].append(var)
        elif 'WASM' in name:
            categories['WASM'].append(var)
        elif 'INTEL' in name and ('GPU' in name or 'XPU' in name):
            categories['Intel ARC/GPU'].append(var)
        elif 'AWS' in name or 'CLOUD' in name:
            categories['Cloud'].append(var)
        elif 'DATABASE' in name or 'REDIS' in name:
            categories['Database'].append(var)
        elif 'PROMETHEUS' in name or 'GRAFANA' in name or 'NODE_EXPORTER' in name:
            categories['Monitoring'].append(var)
        elif 'DOCKER' in name:
            categories['Docker'].append(var)
        elif 'DEBUG' in name or 'LOG' in name or 'PORT' in name or 'WORKER' in name:
            categories['Development'].append(var)
        elif any(sec in name for sec in ['SECRET', 'JWT', 'ENCRYPTION', 'KEY']):
            categories['Security'].append(var)
        elif any(perm in name for perm in ['MAX_', 'BATCH', 'TIMEOUT', 'RETRY']):
            categories['Performance'].append(var)
        elif 'INTEL_CLOUD' in name:
            categories['Intel Cloud'].append(var)
        elif 'INTEL_QUANTUM' in name or 'INTEL_QS' in name:
            categories['Intel Quantum'].append(var)
        else:
            categories['Other'].append(var)
    
    # Generate markdown
    markdown = """# Environment Variables

This document provides a comprehensive reference for all environment variables used in the LLM Pract project.

## Table of Contents

"""
    
    # Add TOC
    for category, vars_list in categories.items():
        if vars_list:
            anchor = category.lower().replace(' ', '-').replace('/', '-')
            markdown += f"- [{category}](#{anchor})\n"
    
    markdown += "\n"
    
    # Generate tables for each category
    for category, vars_list in categories.items():
        if not vars_list:
            continue
            
        markdown += f"## {category}\n\n"
        
        # Table header
        markdown += "| Variable | Type | Default | Required | Description |\n"
        markdown += "|----------|------|---------|----------|-------------|\n"
        
        # Table rows
        for var in sorted(vars_list, key=lambda x: x.name):
            required_str = "Yes" if var.required else "No"
            sensitive_str = "🔒" if var.sensitive else ""
            default_str = var.default if var.default else "None"
            
            # Format description (limit length for table readability)
            desc = var.description
            if len(desc) > 80:
                desc = desc[:77] + "..."
            
            markdown += f"| `{var.name}` {sensitive_str} | {var.var_type} | `{default_str}` | {required_str} | {desc} |\n"
        
        markdown += "\n"
    
    # Add usage notes
    markdown += """## Usage Notes

### Sensitive Variables

Variables marked with 🔒 are sensitive and should never be committed to version control:
- `IBM_QUANTUM_API_KEY`
- `AWS_SECRET_ACCESS_KEY`
- `SECRET_KEY`
- `JWT_SECRET`
- `ENCRYPTION_KEY`
- `INTEL_CLOUD_API_KEY`

### Configuration Sources

Environment variables can be set through:
1. **Environment**: Directly in your shell or CI/CD pipeline
2. **.env files**: For local development (ensure .env files are in .gitignore)
3. **Secrets management**: Use tools like HashiCorp Vault, AWS Secrets Manager, or Kubernetes secrets for production

### Variable Precedence

When the same variable is defined in multiple places, the precedence (from highest to lowest) is:
1. Direct environment variable
2. .env.local
3. .env.development/.env.production
4. .env

### Development vs Production

Some variables have different requirements in development vs production:
- `DEBUG`: Should be `false` in production
- `LOG_LEVEL`: Consider `WARNING` or `ERROR` in production
- Sensitive variables: Always use secrets management in production

## Related Documentation

- [Configuration Guide](Configuration.md)
- [Development Guide](wiki/Development.md)
- [Deployment Guide](wiki/Deployment-Guide.md)
"""
    
    return markdown

def main():
    """Main function to generate environment variables documentation."""
    # Paths
    schema_file = ".env.schema"
    output_file = "docs/environment-variables.md"
    
    # Check if schema file exists
    if not os.path.exists(schema_file):
        print(f"Error: {schema_file} not found!")
        print("Please ensure you're running this script from the project root.")
        return 1
    
    try:
        # Parse environment variables from schema
        print(f"Parsing {schema_file}...")
        env_vars = parse_env_schema(schema_file)
        
        print(f"Found {len(env_vars)} environment variables")
        
        # Generate markdown documentation
        print("Generating markdown documentation...")
        markdown_content = generate_markdown_table(env_vars)
        
        # Write to output file
        os.makedirs(os.path.dirname(output_file), exist_ok=True)
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(markdown_content)
        
        print(f"Successfully generated {output_file}")
        print(f"Documentation includes {len(env_vars)} environment variables")
        
        return 0
        
    except Exception as e:
        print(f"Error generating documentation: {e}")
        return 1

if __name__ == "__main__":
    exit(main())