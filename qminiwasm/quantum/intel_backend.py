"""Intel Quantum computing support.

This module provides integration points for Intel's quantum computing stack, aligned with:
https://www.intel.com/content/www/us/en/research/quantum-computing.html

Intel's ecosystem includes:
- Intel Quantum SDK: full-stack quantum computing in simulation (Python API, IQS and
  Silicon Spin Qubit Simulator backends)
- Tunnel Falls: Intel's silicon spin qubit chip for research
- Intel Quantum Simulator (IQS / intel-qs): high-performance state-vector simulator
- Horse Ridge: cryogenic quantum control; cryoprober for high-volume testing

Use get_intel_quantum_info() for links and capabilities; optional backends are used
when Intel Quantum SDK or intel-qs is installed.
"""

from __future__ import annotations

import logging
from typing import Any, Dict, Optional

logger = logging.getLogger(__name__)

# Intel quantum research and SDK resources (from intel.com quantum-computing page)
INTEL_QUANTUM_RESEARCH_URL = "https://www.intel.com/content/www/us/en/research/quantum-computing.html"
INTEL_QUANTUM_SDK_OVERVIEW_URL = "https://www.intel.com/content/www/us/en/developer/tools/quantum-sdk/overview.html"
INTEL_QUANTUM_SDK_DOCS_URL = "https://intel.github.io/quantum-sdk-docs/"
INTEL_QS_DOCS_URL = "https://intel-qs.readthedocs.io/"


def get_intel_quantum_info() -> Dict[str, Any]:
    """Return information and links for Intel quantum computing support.

    Returns:
        Dict with 'research_url', 'sdk_overview_url', 'sdk_docs_url', 'iqs_docs_url',
        'backends_available' (list of detected backend names), and 'description'.
    """
    backends: list[str] = []
    # Optional: detect Intel Quantum SDK or intel-qs if installed
    try:
        import intel_quantum_sdk  # type: ignore  # noqa: F401
        backends.append("intel_quantum_sdk")
    except ImportError:
        pass
    try:
        import qs  # intel-qs Python bindings  # noqa: F401
        backends.append("intel_qs")
    except ImportError:
        pass
    return {
        "research_url": INTEL_QUANTUM_RESEARCH_URL,
        "sdk_overview_url": INTEL_QUANTUM_SDK_OVERVIEW_URL,
        "sdk_docs_url": INTEL_QUANTUM_SDK_DOCS_URL,
        "iqs_docs_url": INTEL_QS_DOCS_URL,
        "backends_available": backends,
        "description": (
            "Intel quantum practicality: Tunnel Falls silicon spin qubits, "
            "Intel Quantum SDK (simulation stack), Intel Quantum Simulator (IQS), "
            "Horse Ridge cryogenic control."
        ),
    }


def get_intel_quantum_simulator_backend() -> Optional[Any]:
    """Return an Intel Quantum Simulator (IQS) backend if available.

    Requires intel-qs to be built with Python bindings and installed.
    Returns None if not available.
    """
    try:
        import qs  # intel-qs  # noqa: F401
        return qs
    except ImportError:
        return None


__all__ = [
    "INTEL_QUANTUM_RESEARCH_URL",
    "INTEL_QUANTUM_SDK_OVERVIEW_URL",
    "INTEL_QUANTUM_SDK_DOCS_URL",
    "INTEL_QS_DOCS_URL",
    "get_intel_quantum_info",
    "get_intel_quantum_simulator_backend",
]
