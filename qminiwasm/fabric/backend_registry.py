"""Quantum Backend Registry for Q-Mini-WASM

This module provides the backend registry for quantum computing backends.
It handles:
- Backend registration and management
- API key management for external quantum providers
- Backend selection and configuration
"""

import logging
from typing import Any, Dict, List, Optional, Protocol


class QuantumBackend(Protocol):
    """Protocol for interconnect backends that support run_forward."""

    def run_forward(
        self,
        compressed_state: Any,
        gammas: Any,
        betas: Any,
    ) -> Any: ...


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
            backend_type: Type of backend (e.g., 'local', 'pennylane')
            api_key: API key for external providers
        """
        self.backends[name] = {"type": backend_type, "api_key": api_key, "initialized": False}
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
        backend_type = backend_info["type"]

        try:
            if backend_type == "local":
                # Local simulator - no initialization needed
                backend_info["initialized"] = True
                self.logger.info("Initialized local backend: %s", name)

            elif backend_type == "pennylane":
                try:
                    import pennylane  # noqa: F401

                    backend_info["initialized"] = True
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
        if not backend_info["initialized"]:
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
            self.backends[name]["api_key"] = api_key
            self.api_keys[name] = api_key
            self.logger.info("Updated API key for backend: %s", name)
            # Re-initialize backend with new key
            self.initialize_backend(name)
        else:
            self.logger.error("Backend %s not registered", name)


# Global backend registry instance
backend_registry = QuantumBackendRegistry()


class _DefaultQuantumBackend:
    """Minimal backend for interconnect: run_forward returns routing weights (zeros stub)."""

    def __init__(
        self,
        backend_id: str,
        num_qubits: int = 4,
        qaoa_layers: int = 3,
        diff_method: str = "parameter-shift",
    ):
        self.backend_id = backend_id
        self.num_qubits = num_qubits
        self.qaoa_layers = qaoa_layers
        self.diff_method = diff_method

    def run_forward(self, compressed_state: Any, gammas: Any, betas: Any) -> Any:
        try:
            import torch

            if isinstance(compressed_state, torch.Tensor):
                out = torch.zeros(
                    compressed_state.shape[0],
                    self.num_qubits,
                    device=compressed_state.device,
                    dtype=compressed_state.dtype,
                )
                return out
            # numpy or other: return slice
            return compressed_state[:, : self.num_qubits]
        except Exception as e:
            logger.warning("run_forward fallback (compressed_state slice): %s", e, exc_info=False)
            return compressed_state[:, : self.num_qubits]


def get_backend(
    backend_id: str,
    num_qubits: int = 4,
    qaoa_layers: int = 3,
    diff_method: str = "parameter-shift",
) -> QuantumBackend:
    """Factory for interconnect: returns a backend that supports run_forward."""
    return _DefaultQuantumBackend(
        backend_id=backend_id,
        num_qubits=num_qubits,
        qaoa_layers=qaoa_layers,
        diff_method=diff_method,
    )


# Predefined backends
backend_registry.register_backend("local_simulator", "local")
backend_registry.register_backend("pennylane", "pennylane")
