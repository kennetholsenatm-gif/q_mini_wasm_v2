#!/usr/bin/env python3
"""
Agent Recompilation Script

This script handles the recompilation/reinitialization of all AI agents.
It reinstalls dependencies, validates configurations, reinitializes agents,
and performs health checks.
"""

import os
import sys
import subprocess
import json
import logging
from pathlib import Path
from typing import Dict, List, Optional
import importlib
import traceback

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class AgentRecompiler:
    """Handles recompilation of all AI agents."""
    
    def __init__(self, project_root: Optional[str] = None):
        """Initialize the recompiler.
        
        Args:
            project_root: Root directory of the project. If None, uses current directory.
        """
        self.project_root = Path(project_root) if project_root else Path.cwd()
        self.agents_dir = self.project_root / "agents"
        self.config_path = self.agents_dir / "config.json"
        self.requirements_path = self.agents_dir / "requirements.txt"
        
        # Track results
        self.results = {
            "dependencies_installed": False,
            "config_validated": False,
            "agents_recompiled": [],
            "health_checks": {},
            "errors": []
        }
    
    def install_dependencies(self) -> bool:
        """Install agent dependencies.
        
        Returns:
            True if successful, False otherwise.
        """
        logger.info("Installing agent dependencies...")
        
        if not self.requirements_path.exists():
            logger.warning(f"Requirements file not found: {self.requirements_path}")
            return True  # Not an error if no requirements.txt
        
        try:
            # Upgrade pip first
            subprocess.run(
                [sys.executable, "-m", "pip", "install", "--upgrade", "pip"],
                check=True,
                capture_output=True
            )
            
            # Install requirements
            result = subprocess.run(
                [sys.executable, "-m", "pip", "install", "-r", str(self.requirements_path)],
                check=True,
                capture_output=True,
                text=True
            )
            
            logger.info("Dependencies installed successfully")
            self.results["dependencies_installed"] = True
            return True
            
        except subprocess.CalledProcessError as e:
            error_msg = f"Failed to install dependencies: {e.stderr}"
            logger.error(error_msg)
            self.results["errors"].append(error_msg)
            return False
    
    def validate_config(self) -> bool:
        """Validate agent configuration.
        
        Returns:
            True if valid, False otherwise.
        """
        logger.info("Validating agent configuration...")
        
        if not self.config_path.exists():
            error_msg = f"Config file not found: {self.config_path}"
            logger.error(error_msg)
            self.results["errors"].append(error_msg)
            return False
        
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                config = json.load(f)
            
            # Basic validation
            required_sections = ["agents", "gemini", "improvement_cycle"]
            for section in required_sections:
                if section not in config:
                    error_msg = f"Missing required config section: {section}"
                    logger.error(error_msg)
                    self.results["errors"].append(error_msg)
                    return False
            
            # Validate agents section
            if not isinstance(config["agents"], dict):
                error_msg = "Config 'agents' section must be a dictionary"
                logger.error(error_msg)
                self.results["errors"].append(error_msg)
                return False
            
            logger.info("Configuration validated successfully")
            self.results["config_validated"] = True
            return True
            
        except json.JSONDecodeError as e:
            error_msg = f"Invalid JSON in config file: {e}"
            logger.error(error_msg)
            self.results["errors"].append(error_msg)
            return False
        except Exception as e:
            error_msg = f"Error validating config: {e}"
            logger.error(error_msg)
            self.results["errors"].append(error_msg)
            return False
    
    def recompile_agent(self, agent_name: str, agent_config: Dict) -> bool:
        """Recompile a single agent.
        
        Args:
            agent_name: Name of the agent.
            agent_config: Configuration for the agent.
            
        Returns:
            True if successful, False otherwise.
        """
        logger.info(f"Recompiling agent: {agent_name}")
        
        try:
            # Import the agent module dynamically
            module_name = f"agents.{agent_name.lower()}"
            if agent_name.lower() == "documentation_agent":
                module_name = "agents.documentation_agent"
            elif agent_name.lower() == "kanban_review_fix_agent":
                module_name = "agents.kanban_review_fix_agent"
            
            try:
                # Try to import and reload the module
                module = importlib.import_module(module_name)
                importlib.reload(module)
                logger.info(f"Successfully reloaded module: {module_name}")
            except ImportError as e:
                logger.warning(f"Could not import agent module {module_name}: {e}")
            
            # Import the agents package to reinitialize
            try:
                import agents
                importlib.reload(agents)
                logger.info("Reloaded agents package")
            except Exception as e:
                logger.warning(f"Could not reload agents package: {e}")
            
            # Perform agent-specific reinitialization
            self._reinitialize_agent_memory(agent_name)
            
            # Log success
            self.results["agents_recompiled"].append({
                "name": agent_name,
                "status": "success",
                "timestamp": str(Path(__file__).stat().st_mtime)
            })
            
            return True
            
        except Exception as e:
            error_msg = f"Failed to recompile agent {agent_name}: {e}"
            logger.error(error_msg)
            self.results["errors"].append(error_msg)
            
            self.results["agents_recompiled"].append({
                "name": agent_name,
                "status": "failed",
                "error": str(e),
                "timestamp": str(Path(__file__).stat().st_mtime)
            })
            
            return False