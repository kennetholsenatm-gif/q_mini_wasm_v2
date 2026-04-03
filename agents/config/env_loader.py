"""
Environment Configuration Loader

This module provides utilities for loading environment variables from .env files
and managing configuration across the agent ecosystem.
"""

import os
import json
from pathlib import Path
from typing import Any, Dict, Optional, Union
import structlog

logger = structlog.get_logger()

# Try to import dotenv, but make it optional
try:
    from dotenv import load_dotenv
    DOTENV_AVAILABLE = True
except ImportError:
    DOTENV_AVAILABLE = False
    logger.warning("python-dotenv not installed, .env files will not be loaded automatically")


class EnvConfig:
    """Environment configuration manager."""
    
    def __init__(self, env_file: Optional[Union[str, Path]] = None, auto_load: bool = True):
        """
        Initialize environment configuration.
        
        Args:
            env_file: Path to .env file. If None, searches for .env in current and parent directories.
            auto_load: Whether to automatically load the .env file on initialization.
        """
        self.env_file = Path(env_file) if env_file else None
        self._loaded = False
        
        if auto_load:
            self.load()
    
    def load(self, env_file: Optional[Union[str, Path]] = None) -> bool:
        """
        Load environment variables from .env file.
        
        Args:
            env_file: Optional specific .env file to load. Overrides the one set in __init__.
            
        Returns:
            True if successful, False otherwise.
        """
        if not DOTENV_AVAILABLE:
            logger.warning("Cannot load .env file: python-dotenv not available")
            return False
        
        target_file = Path(env_file) if env_file else self.env_file
        
        # If no specific file provided, search for .env files
        if target_file is None:
            target_file = self._find_env_file()
        
        if target_file and target_file.exists():
            try:
                load_dotenv(dotenv_path=target_file, override=False)
                self.env_file = target_file
                self._loaded = True
                logger.info(f"Loaded environment from {target_file}")
                return True
            except Exception as e:
                logger.error(f"Failed to load .env file {target_file}: {e}")
                return False
        else:
            logger.debug("No .env file found or specified")
            return False
    
    def _find_env_file(self) -> Optional[Path]:
        """
        Search for .env file in current directory and parent directories.
        
        Returns:
            Path to .env file if found, None otherwise.
        """
        current_dir = Path.cwd()
        
        # Check current directory first
        env_path = current_dir / ".env"
        if env_path.exists():
            return env_path
        
        # Check parent directories up to 3 levels
        for _ in range(3):
            current_dir = current_dir.parent
            env_path = current_dir / ".env"
            if env_path.exists():
                return env_path
        
        # Check for environment-specific files
        env_name = os.getenv("ENVIRONMENT", "development")
        for env_suffix in [f".env.{env_name}", ".env.local", ".env.development", ".env.production"]:
            env_path = Path.cwd() / env_suffix
            if env_path.exists():
                return env_path
        
        return None
    
    def get(self, key: str, default: Any = None) -> Any:
        """
        Get environment variable value.
        
        Args:
            key: Environment variable name.
            default: Default value if not found.
            
        Returns:
            Environment variable value or default.
        """
        return os.getenv(key, default)
    
    def get_bool(self, key: str, default: bool = False) -> bool:
        """
        Get boolean environment variable.
        
        Args:
            key: Environment variable name.
            default: Default value if not found.
            
        Returns:
            Boolean value of environment variable.
        """
        value = os.getenv(key)
        if value is None:
            return default
        
        return value.lower() in ("true", "1", "yes", "on")
    
    def get_int(self, key: str, default: int = 0) -> int:
        """
        Get integer environment variable.
        
        Args:
            key: Environment variable name.
            default: Default value if not found.
            
        Returns:
            Integer value of environment variable.
        """
        value = os.getenv(key)
        if value is None:
            return default
        
        try:
            return int(value)
        except ValueError:
            logger.warning(f"Invalid integer value for {key}: {value}")
            return default
    
    def get_float(self, key: str, default: float = 0.0) -> float:
        """
        Get float environment variable.
        
        Args:
            key: Environment variable name.
            default: Default value if not found.
            
        Returns:
            Float value of environment variable.
        """
        value = os.getenv(key)
        if value is None:
            return default
        
        try:
            return float(value)
        except ValueError:
            logger.warning(f"Invalid float value for {key}: {value}")
            return default
    
    def get_json(self, key: str, default: Any = None) -> Any:
        """
        Get JSON environment variable.
        
        Args:
            key: Environment variable name.
            default: Default value if not found or invalid JSON.
            
        Returns:
            Parsed JSON value or default.
        """
        value = os.getenv(key)
        if value is None:
            return default
        
        try:
            return json.loads(value)
        except json.JSONDecodeError:
            logger.warning(f"Invalid JSON for {key}: {value}")
            return default
    
    def require(self, key: str) -> str:
        """
        Get required environment variable.
        
        Args:
            key: Environment variable name.
            
        Returns:
            Environment variable value.
            
        Raises:
            ValueError: If environment variable is not set.
        """
        value = os.getenv(key)
        if value is None:
            raise ValueError(f"Required environment variable {key} is not set")
        return value
    
    def list_variables(self) -> Dict[str, str]:
        """
        List all environment variables (excluding system variables).
        
        Returns:
            Dictionary of environment variables.
        """
        # Filter out system variables for security
        system_vars = {
            "PATH", "PYTHONPATH", "JAVA_HOME", "NODE_PATH", "GOPATH",
            "USER", "HOME", "SHELL", "LANG", "LC_ALL", "TERM",
            "HOSTNAME", "HOSTTYPE", "MACHTYPE", "OSTYPE"
        }
        
        return {
            k: v for k, v in os.environ.items()
            if k not in system_vars and not k.startswith("_")
        }
    
    def is_loaded(self) -> bool:
        """Check if .env file was loaded."""
        return self._loaded
    
    def get_env_file_path(self) -> Optional[Path]:
        """Get the path to the loaded .env file."""
        return self.env_file


# Global instance for convenience
env_config = EnvConfig()


def get_env(key: str, default: Any = None) -> Any:
    """Get environment variable using global config."""
    return env_config.get(key, default)


def require_env(key: str) -> str:
    """Get required environment variable using global config."""
    return env_config.require(key)


def load_env_from_path(path: Union[str, Path]) -> bool:
    """
    Load environment variables from specific path.
    
    Args:
        path: Path to .env file.
        
    Returns:
        True if successful, False otherwise.
    """
    return env_config.load(path)


def find_all_env_files(start_path: Optional[Union[str, Path]] = None) -> list:
    """
    Find all .env files in directory tree.
    
    Args:
        start_path: Starting directory. Defaults to current directory.
        
    Returns:
        List of paths to .env files found.
    """
    start_path = Path(start_path) if start_path else Path.cwd()
    env_files = []
    
    # Common env file patterns
    patterns = [".env", ".env.*", ".env.local", ".env.development", ".env.production", ".env.test"]
    
    for pattern in patterns:
        env_files.extend(start_path.rglob(pattern))
    
    # Also check for .env files in common locations
    common_locations = [
        start_path / ".env",
        start_path / "config" / ".env",
        start_path / "config" / ".env.local",
        start_path / "secrets" / ".env",
        start_path / "env" / ".env",
    ]
    
    for location in common_locations:
        if location.exists() and location not in env_files:
            env_files.append(location)
    
    return env_files


def scan_for_env_variables(directory: Optional[Union[str, Path]] = None) -> Dict[str, list]:
    """
    Scan directory for environment variable usage in code files.
    
    Args:
        directory: Directory to scan. Defaults to current directory.
        
    Returns:
        Dictionary mapping file paths to lists of environment variable names found.
    """
    import re
    
    directory = Path(directory) if directory else Path.cwd()
    env_vars_by_file = {}
    
    # Patterns to match environment variable usage
    patterns = [
        r'os\.getenv\(["\']([^"\']+)["\']',  # os.getenv("VAR")
        r'os\.environ\[["\']([^"\']+)["\']\]',  # os.environ["VAR"]
        r'os\.environ\.get\(["\']([^"\']+)["\']',  # os.environ.get("VAR")
        r'process\.env\.([A-Z_][A-Z0-9_]*)',  # process.env.VAR (Node.js)
        r'\$\{([A-Z_][A-Z0-9_]*)\}',  # ${VAR} (shell/config)
        r'%([A-Z_][A-Z0-9_]*)%',  # %VAR% (Windows)
    ]
    
    for py_file in directory.rglob("*.py"):
        try:
            content = py_file.read_text(encoding="utf-8")
            env_vars = set()
            
            for pattern in patterns:
                matches = re.findall(pattern, content)
                env_vars.update(matches)
            
            if env_vars:
                env_vars_by_file[str(py_file)] = sorted(list(env_vars))
        except Exception as e:
            logger.warning(f"Could not read {py_file}: {e}")
    
    return env_vars_by_file