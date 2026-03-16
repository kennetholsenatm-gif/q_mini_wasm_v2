"""Quantum Backend Registry for Q-Mini-WASM

This module provides the backend registry for quantum computing backends.
It handles:
- Backend registration and management
- API key management for external quantum providers
- Backend selection and configuration
"""

import logging
from typing import Dict, Optional, Any
from qiskit import IBMQ
from qiskit.providers.ibmq import IBMQProvider

logger = logging.getLogger(__name__)


class QuantumBackendRegistry:
    """Quantum Backend Registry for managing quantum computing backends"""

    def __init__(self):
        """Initialize the backend registry"""
        self.backends = {}
        self.api_keys = {}
        self.logger = logging.getLogger(__name__)
        self._initialized = False

    def register_backend(self, name: str, backend_type: str, api_key: Optional[str] = None):
        """Register a quantum backend

        Args:
            name: Name of the backend
            backend_type: Type of backend (e.g., 'ibmq', 'local', 'pennylane')
            api_key: API key for external providers
        """
        self.backends[name] = {
            'type': backend_type,
            'api_key': api_key,
            'initialized': False
        }
        self.api_keys[name] = api_key
        self.logger.info("Registered backend: %s (%s)", name, backend_type)

    def initialize_backend(self, name: str):
        """Initialize a registered backend

        Args:
            name: Name of the backend to initialize
        """
        if name not in self.backends:
            self.logger.error("Backend %s not registered", name)
            return False

        backend_info = self.backends[name]
        backend_type = backend_info['type']
        api_key = backend_info['api_key']

        try:
            if backend_type == 'ibmq':
                if api_key:
                    IBMQ.save_account(api_key, overwrite=True)
                    provider = IBMQ.load_account()
                    backend_info['provider'] = provider
                    backend_info['initialized'] = True
                    self.logger.info("Initialized IBM Quantum backend: %s", name)
                else:
                    self.logger.warning("No API key provided for IBM Quantum backend")

            elif backend_type == 'local':
                # Local simulator - no initialization needed
                backend_info['initialized'] = True
                self.logger.info("Initialized local backend: %s", name)

            elif backend_type == 'pennylane':
                try:
                    import pennylane as qml
                    backend_info['initialized'] = True
                    self.logger.info("Initialized PennyLane backend: %s", name)
                except ImportError:
                    self.logger.warning("PennyLane not available for backend: %s", name)

            else:
                self.logger.warning("Unknown backend type: %s", backend_type)

        except Exception as e:
            self.logger.error("Failed to initialize backend %s: %s", name, str(e))
            return False

        return True

    def get_backend(self, name: str) -> Optional[Dict[str, Any]]:
        """Get a backend by name

        Args:
            name: Name of the backend to retrieve

        Returns:
            Backend information if available, None otherwise
        """
        if name not in self.backends:
            self.logger.error("Backend %s not registered", name)
            return None

        backend_info = self.backends[name]
        if not backend_info['initialized']:
            self.logger.warning("Backend %s not initialized", name)
            return None

        return backend_info

    def list_backends(self) -> List[str]:
        """List all registered backends"""
        return list(self.backends.keys())

    def remove_backend(self, name: str):
        """Remove a backend

        Args:
            name: Name of the backend to remove
        """
        if name in self.backends:
            del self.backends[name]
            if name in self.api_keys:
                del self.api_keys[name]
            self.logger.info("Removed backend: %s", name)

    def update_api_key(self, name: str, api_key: str):
        """Update API key for a backend

        Args:
            name: Name of the backend
            api_key: New API key
        """
        if name in self.backends:
            self.backends[name]['api_key'] = api_key
            self.api_keys[name] = api_key
            self.logger.info("Updated API key for backend: %s", name)
            # Re-initialize backend with new key
            self.initialize_backend(name)
        else:
            self.logger.error("Backend %s not registered", name)


# Global backend registry instance
backend_registry = QuantumBackendRegistry()

# Predefined backends
backend_registry.register_backend('ibmq_qasm_simulator', 'ibmq')
backend_registry.register_backend('local_simulator', 'local')
backend_registry.register_backend('pennylane', 'pennylane')