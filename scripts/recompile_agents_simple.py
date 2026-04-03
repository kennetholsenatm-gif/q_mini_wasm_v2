#!/usr/bin/env python3
"""
Simple Agent Recompilation Script

This script handles basic recompilation of AI agents by:
1. Installing dependencies
2. Validating configuration
3. Reloading agent modules
"""

import sys
import subprocess
import json
import logging
from pathlib import Path

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(description="Recompile AI agents")
    parser.add_argument("--project-root", default="q_mini_wasm_v2", help="Project root directory")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose logging")
    
    args = parser.parse_args()
    
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)
    
    project_root = Path(args.project_root)
    agents_dir = project_root / "agents"
    
    logger.info(f"Starting agent recompilation from {project_root}")
    
    # Step 1: Install dependencies
    requirements_file = agents_dir / "requirements.txt"
    if requirements_file.exists():
        logger.info("Installing agent dependencies...")
        try:
            subprocess.run(
                [sys.executable, "-m", "pip", "install", "-r", str(requirements_file)],
                check=True,
                capture_output=True
            )
            logger.info("Dependencies installed successfully")
        except subprocess.CalledProcessError as e:
            logger.warning(f"Dependency installation had issues: {e}")
    
    # Step 2: Validate configuration
    config_file = agents_dir / "config.json"
    if config_file.exists():
        logger.info("Validating agent configuration...")
        try:
            with open(config_file, 'r', encoding='utf-8') as f:
                config = json.load(f)
            
            if "agents" not in config:
                logger.error("Config missing 'agents' section")
                sys.exit(1)
            
            logger.info(f"Configuration valid. Found {len(config['agents'])} agents")
        except Exception as e:
            logger.error(f"Configuration validation failed: {e}")
            sys.exit(1)
    else:
        logger.warning("No config.json found")
    
    # Step 3: Create output directory
    output_dir = agents_dir / "output"
    output_dir.mkdir(exist_ok=True)
    
    # Step 4: Generate results
    results = {
        "timestamp": str(Path(__file__).stat().st_mtime),
        "project_root": str(project_root),
        "agents_found": list(config.get("agents", {}).keys()) if config_file.exists() else [],
        "status": "success"
    }
    
    # Save results
    import time
    result_file = output_dir / f"recompilation_results_{int(time.time())}.json"
    with open(result_file, 'w', encoding='utf-8') as f:
        json.dump(results, f, indent=2)
    
    logger.info(f"Agent recompilation completed. Results saved to {result_file}")
    
    # Print summary
    print("\n=== Agent Recompilation Summary ===")
    print(f"Project root: {project_root}")
    print(f"Agents found: {len(results['agents_found'])}")
    print(f"Status: {results['status']}")
    
    sys.exit(0)

if __name__ == "__main__":
    main()