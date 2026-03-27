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
        self.required = False


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
    pending_type = "string"
    pending_sensitive = False

    with open(schema_file, "r", encoding="utf-8") as f:
        lines = f.readlines()

    for line in lines:
        line = line.strip()

        # Skip empty lines and comments that aren't part of Varlock spec
        if not line or line.startswith("#") and not line.startswith("# @"):
            continue

        # Check for Varlock annotations
        if line.startswith("@env-spec"):
            if current_var:
                # Save previous variable
                current_var.description = " ".join(current_description).strip()
                env_vars.append(current_var)

            # Start new variable (we'll get the name from the next line)
            current_var = None
            current_description = []
            pending_type = "string"
            pending_sensitive = False

        elif line.startswith("@description"):
            if current_var:
                desc = line.replace("@description", "").strip()
                current_description.append(desc)
            else:
                # Store description for next variable
                current_description.append(line.replace("@description", "").strip())

        elif line.startswith("@type"):
            t = line.replace("@type", "").strip()
            if current_var:
                current_var.var_type = t
            else:
                pending_type = t

        elif line.startswith("@default"):
            if current_var:
                current_var.default = line.replace("@default", "").strip()

        elif line.startswith("@sensitive"):
            sens = line.replace("@sensitive", "").strip().lower() == "true"
            if current_var:
                current_var.sensitive = sens
            else:
                pending_sensitive = sens

        elif "=" in line and not line.startswith("@"):
            # This should be the variable definition
            name = line.split("=")[0].strip()
            if current_var is None:
                current_var = EnvVar(name)
                current_var.var_type = pending_type
                current_var.sensitive = pending_sensitive
            else:
                current_var.name = name

            # Set description from accumulated lines
            current_var.description = " ".join(current_description).strip()

            # Add to list and reset
            env_vars.append(current_var)
            current_var = None
            current_description = []
            pending_type = "string"
            pending_sensitive = False

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

    # Group credentials only (.env.schema is secrets/API keys — not app configuration)
    categories = {
        "Hugging Face": [],
        "IBM Quantum": [],
        "RunPod": [],
        "Other": [],
    }

    for var in env_vars:
        name = var.name.upper()
        if name in ("HUGGING_FACE_HUB_TOKEN", "HF_TOKEN"):
            categories["Hugging Face"].append(var)
        elif "IBM_QUANTUM" in name or "QISKIT_IBM" in name:
            categories["IBM Quantum"].append(var)
        elif "RUNPOD" in name:
            categories["RunPod"].append(var)
        else:
            categories["Other"].append(var)

    # Generate markdown
    markdown = """# Environment variables (secrets and APIs)

**Policy:** Variables documented here are **credentials and API keys** loaded from `.env` (or the process environment). They are **not** how you configure training, inference, accelerators, WASM limits, or feature toggles — use **`configs/training/*.toml`**, **`configs/serve/*.toml`**, and the **[Training WUI](../training-wui/README.md)** for that.

For **CI, Docker, serverless, and toolchain** variables that may still be read by the codebase, see **[ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md)**.

## Table of Contents

"""

    # Add TOC
    for category, vars_list in categories.items():
        if vars_list:
            anchor = category.lower().replace(" ", "-").replace("/", "-")
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
    markdown += """## Usage notes

- Treat variables marked with 🔒 as **secrets**: never commit real values; use a secrets manager in production where applicable.
- Copy **[.env.example](../.env.example)** to `.env` for local development (`.env` is gitignored).
- **Do not** add training hyperparameters or runtime toggles to `.env` — add them to TOML or use the WUI.

## Related documentation

- [README](../README.md) — project overview
- [Training data](TRAINING_DATA.md)
- [ENV_CI_OVERRIDES.md](ENV_CI_OVERRIDES.md) — CI / container / legacy process env (not WUI user config)
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
        with open(output_file, "w", encoding="utf-8") as f:
            f.write(markdown_content)

        print(f"Successfully generated {output_file}")
        print(f"Documentation includes {len(env_vars)} environment variables")

        return 0

    except Exception as e:
        print(f"Error generating documentation: {e}")
        return 1


if __name__ == "__main__":
    exit(main())
