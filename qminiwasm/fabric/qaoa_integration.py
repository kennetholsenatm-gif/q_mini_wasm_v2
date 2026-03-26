"""Quantum Approximate Optimization Algorithm (QAOA) Integration

This module implements the Neural Network-initialized QAOA protocol for
ternary hierarchical edge-quantum architecture. It provides:

- Problem Hamiltonian H_C implementation for ternary optimization
- Neural Network angle prediction for γ and β parameters
- Quantum state injection and measurement
- Classical-quantum hybrid optimization loop
- Integration with ternary quantization pipeline

The implementation follows the mathematical formulations from the Q-Mini-WASM
white paper, including problem Hamiltonian construction and angle prediction.
"""

import logging
import hashlib

import numpy as np
import torch
import torch.nn as nn
from dataclasses import dataclass
from typing import Any, Callable, Dict, Optional, Tuple

from .mesh_qubo import prune_topology_from_edge_scores

try:
    import pennylane as qml
    from pennylane import numpy as pnp

    PENNYLANE_AVAILABLE = True
except ImportError:
    PENNYLANE_AVAILABLE = False
    qml = None
    pnp = None

try:
    from qiskit import QuantumCircuit, QuantumRegister, ClassicalRegister
    from qiskit.algorithms import QAOA
    from qiskit.algorithms.optimizers import COBYLA
    from qiskit.quantum_info import SparsePauliOp
    from qiskit.utils import QuantumInstance

    QISKIT_AVAILABLE = True
except ImportError:
    QISKIT_AVAILABLE = False
    QuantumCircuit = QuantumRegister = ClassicalRegister = None
    QAOA = COBYLA = SparsePauliOp = QuantumInstance = None

logger = logging.getLogger(__name__)


@dataclass
class QAOAConfig:
    """Configuration for QAOA integration."""

    num_layers: int = 2
    optimizer_lr: float = 0.01
    max_iterations: int = 100
    quantum_backend: str = "default.qubit"
    use_neural_prediction: bool = True
    angle_prediction_hidden_dim: int = 64
    problem_type: str = "ternary_optimization"
    #: ``pennylane`` | ``qiskit_statevector`` | ``qiskit_ibm`` (no autograd through IBM device).
    execution_mode: str = "pennylane"
    ibm_shots: int = 1024
    ibm_backend_name: Optional[str] = None
    #: Engine / TOML ``[hardware].quantum_backend`` (fallback for IBM device when env is unset).
    engine_quantum_backend: Optional[str] = None
    #: qiskit simulator backend choice: auto | statevector | mps
    simulator_backend: str = "auto"
    qaoa_mps_max_bond_dim: Optional[int] = None
    qaoa_prune_enabled: bool = False
    qaoa_prune_threshold: float = 0.0
    qaoa_prune_min_nodes: int = 4
    qaoa_warm_start_cache_ttl: int = 128
    qaoa_warm_start_max_hamming: float = 0.20


class ProblemHamiltonian(nn.Module):
    """Problem Hamiltonian H_C for ternary optimization.

    This class implements the problem Hamiltonian for ternary weight optimization,
    which encodes the optimization objective into a quantum Hamiltonian.
    """

    def __init__(self, num_qubits: int, problem_type: str = "ternary_optimization"):
        """Initialize the ProblemHamiltonian.

        Args:
            num_qubits: Number of qubits in the system
            problem_type: Type of optimization problem
        """
        super(ProblemHamiltonian, self).__init__()
        self.num_qubits = num_qubits
        self.problem_type = problem_type

        # Initialize problem-specific parameters
        if problem_type == "ternary_optimization":
            # For ternary optimization, energy is minimized
            # based on weight configuration
            self.weight_couplings = nn.Parameter(torch.randn(num_qubits, num_qubits))
            self.bias_terms = nn.Parameter(torch.randn(num_qubits))
        elif problem_type == "routing_optimization":
            # For routing optimization, routing costs are encoded
            self.routing_matrix = nn.Parameter(torch.randn(num_qubits, num_qubits))
        else:
            raise ValueError(f"Unknown problem type: {problem_type}")

    def forward(self, weights: torch.Tensor) -> torch.Tensor:
        """Compute the problem Hamiltonian energy.

        Args:
            weights: Ternary weight tensor

        Returns:
            Hamiltonian energy
        """
        if self.problem_type == "ternary_optimization":
            # Energy based on weight configuration and couplings
            energy = torch.sum(self.weight_couplings * torch.outer(weights, weights))
            energy += torch.sum(self.bias_terms * weights)
        elif self.problem_type == "routing_optimization":
            # Routing cost based on weight configuration
            energy = torch.sum(self.routing_matrix * torch.outer(weights, weights))

        return energy

    def to_pauli_op(self, weights: torch.Tensor) -> Optional[SparsePauliOp]:
        """Convert to Qiskit SparsePauliOp for quantum execution.

        Args:
            weights: Ternary weight tensor

        Returns:
            SparsePauliOp representation
        """
        if not QISKIT_AVAILABLE:
            return None

        # Convert to Pauli operators
        pauli_strings = []
        coefficients = []

        # Add Z terms for bias
        for i in range(self.num_qubits):
            pauli_str = ["I"] * self.num_qubits
            pauli_str[i] = "Z"
            pauli_strings.append("".join(pauli_str))
            coefficients.append(self.bias_terms[i].item())

        # Add ZZ terms for couplings
        for i in range(self.num_qubits):
            for j in range(i + 1, self.num_qubits):
                pauli_str = ["I"] * self.num_qubits
                pauli_str[i] = "Z"
                pauli_str[j] = "Z"
                pauli_strings.append("".join(pauli_str))
                coefficients.append(self.weight_couplings[i, j].item())

        return SparsePauliOp(pauli_strings, coefficients)


class AnglePredictor(nn.Module):
    """Neural Network for predicting QAOA angles γ and β.

    This class implements a neural network that predicts optimal QAOA angles
    based on the current weight configuration, enabling faster convergence.
    """

    def __init__(self, input_dim: int, hidden_dim: int, num_layers: int, output_dim: int):
        """Initialize the AnglePredictor.

        Args:
            input_dim: Input dimension (typically weight dimension)
            hidden_dim: Hidden layer dimension
            num_layers: Number of QAOA layers
            output_dim: Output dimension (2 * num_layers for γ and β)
        """
        super(AnglePredictor, self).__init__()
        self.num_layers = num_layers

        # Neural network for angle prediction
        self.network = nn.Sequential(
            nn.Linear(input_dim, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, hidden_dim),
            nn.ReLU(),
            nn.Linear(hidden_dim, output_dim),
        )

        # Initialize with small random values
        self._initialize_angles()

    def _initialize_angles(self):
        """Initialize angle prediction network with small random values."""
        for layer in self.network:
            if isinstance(layer, nn.Linear):
                nn.init.normal_(layer.weight, std=0.01)
                nn.init.constant_(layer.bias, 0.0)

    def forward(self, weights: torch.Tensor) -> torch.Tensor:
        """Predict QAOA angles based on weights.

        Args:
            weights: Current weight configuration

        Returns:
            Predicted angles [γ_1, β_1, ..., γ_p, β_p]
        """
        # Flatten weights if needed
        if weights.dim() > 1:
            weights_flat = weights.view(-1)
        else:
            weights_flat = weights

        # Predict angles
        angles = self.network(weights_flat)

        # Apply constraints to keep angles in reasonable range
        # γ typically in [0, π], β typically in [0, π/2]
        gamma_angles = angles[: self.num_layers] * np.pi
        beta_angles = angles[self.num_layers :] * (np.pi / 2)

        return torch.cat([gamma_angles, beta_angles])


class NeuralQAOA(nn.Module):
    """Neural Network-initialized QAOA protocol.

    This class implements the complete QAOA protocol with neural network
    angle prediction for faster convergence and better optimization.
    """

    def __init__(self, num_qubits: int, config: QAOAConfig, weights: Optional[torch.Tensor] = None):
        """Initialize the NeuralQAOA.

        Args:
            num_qubits: Number of qubits in the system
            config: QAOA configuration
            weights: Initial weight configuration
        """
        super(NeuralQAOA, self).__init__()
        self.num_qubits = num_qubits
        self.config = config
        self._angle_cache: Dict[str, Tuple[torch.Tensor, torch.Tensor]] = {}

        # Initialize problem Hamiltonian
        self.problem_hamiltonian = ProblemHamiltonian(num_qubits, config.problem_type)

        # Initialize angle predictor
        if config.use_neural_prediction:
            self.angle_predictor = AnglePredictor(
                input_dim=num_qubits,
                hidden_dim=config.angle_prediction_hidden_dim,
                num_layers=config.num_layers,
                output_dim=2 * config.num_layers,
            )
        else:
            self.angle_predictor = None

        # Quantum circuit parameters (must exist before _initialize_random_angles)
        self.gamma = nn.Parameter(torch.randn(config.num_layers))
        self.beta = nn.Parameter(torch.randn(config.num_layers))

        # Initialize angles
        if weights is not None:
            self.register_buffer("current_weights", weights)
            self._initialize_angles_from_weights(weights)
        else:
            self.register_buffer("current_weights", torch.zeros(num_qubits))
            self._initialize_random_angles()

    def _initialize_angles_from_weights(self, weights: torch.Tensor):
        """Initialize angles based on current weights using neural prediction."""
        if self.angle_predictor is not None:
            predicted_angles = self.angle_predictor(weights)
            with torch.no_grad():
                self.gamma.copy_(predicted_angles[: self.config.num_layers])
                self.beta.copy_(predicted_angles[self.config.num_layers :])
        else:
            self._initialize_random_angles()

    @staticmethod
    def _topology_fingerprint(weights: torch.Tensor) -> str:
        w = weights.detach().to(dtype=torch.float32).reshape(-1).cpu().numpy()
        sign = np.sign(w).astype(np.int8).tobytes()
        # Non-crypto topology cache key (not authentication); SHA-256 avoids weak-hash lint noise.
        return hashlib.sha256(sign).hexdigest()

    def _maybe_prune_weights(self, weights: torch.Tensor) -> torch.Tensor:
        if not bool(getattr(self.config, "qaoa_prune_enabled", False)):
            return weights
        flat = weights.reshape(-1)
        topo = prune_topology_from_edge_scores(
            flat.abs(),
            threshold=float(getattr(self.config, "qaoa_prune_threshold", 0.0)),
            min_edges=max(1, int(getattr(self.config, "qaoa_prune_min_nodes", 4))),
        )
        kept = topo.kept_edge_indices
        if not kept:
            return flat[:1]
        return flat[torch.tensor(kept, device=flat.device)]

    def _warm_start_angles_if_available(self, weights: torch.Tensor) -> None:
        fp = self._topology_fingerprint(weights)
        hit = self._angle_cache.get(fp)
        if hit is not None:
            g, b = hit
            with torch.no_grad():
                if g.shape == self.gamma.shape and b.shape == self.beta.shape:
                    self.gamma.copy_(g.to(device=self.gamma.device, dtype=self.gamma.dtype))
                    self.beta.copy_(b.to(device=self.beta.device, dtype=self.beta.dtype))

    def _update_angle_cache(
        self, weights: torch.Tensor, gamma: torch.Tensor, beta: torch.Tensor
    ) -> None:
        fp = self._topology_fingerprint(weights)
        self._angle_cache[fp] = (gamma.detach().clone(), beta.detach().clone())
        ttl = max(1, int(getattr(self.config, "qaoa_warm_start_cache_ttl", 128)))
        if len(self._angle_cache) > ttl:
            first_key = next(iter(self._angle_cache))
            self._angle_cache.pop(first_key, None)

    def _initialize_random_angles(self):
        """Initialize angles with random values."""
        with torch.no_grad():
            self.gamma.uniform_(0, np.pi)
            self.beta.uniform_(0, np.pi / 2)

    def quantum_circuit(self, weights: torch.Tensor, gamma: torch.Tensor, beta: torch.Tensor):
        """Define the QAOA quantum circuit.

        Args:
            weights: Weight configuration
            gamma: Problem Hamiltonian angles
            beta: Mixer Hamiltonian angles
        """
        mode = getattr(self.config, "execution_mode", "pennylane") or "pennylane"
        if mode in ("qiskit_statevector", "qiskit_ibm", "cpp_cluster_first"):
            return self._quantum_circuit_qiskit(weights, gamma, beta)

        if PENNYLANE_AVAILABLE:
            # PennyLane implementation
            dev = qml.device(self.config.quantum_backend, wires=self.num_qubits)

            @qml.qnode(dev)
            def circuit():
                # Initialize in superposition state
                for i in range(self.num_qubits):
                    qml.Hadamard(wires=i)

                # QAOA layers
                for layer in range(self.config.num_layers):
                    # Problem Hamiltonian evolution
                    self._apply_problem_hamiltonian(weights, gamma[layer])

                    # Mixer Hamiltonian evolution
                    self._apply_mixer_hamiltonian(beta[layer])

                return [qml.expval(qml.PauliZ(i)) for i in range(self.num_qubits)]

            return circuit()
        else:
            # Fallback to classical simulation
            return self._classical_simulation(weights, gamma, beta)

    def _quantum_circuit_qiskit(
        self, weights: torch.Tensor, gamma: torch.Tensor, beta: torch.Tensor
    ) -> torch.Tensor:
        """Same QAOA ansatz as PennyLane, executed via Qiskit (statevector or IBM Runtime)."""
        from qminiwasm.fabric.qiskit_qaoa import resolve_ibm_backend_name, run_z_expectations
        from qminiwasm.fabric.performance_optimizer import solve_qubo_cpp_cluster_first

        w = weights.detach().cpu().numpy().reshape(-1)[: self.num_qubits]
        if w.size < self.num_qubits:
            w = np.pad(w, (0, self.num_qubits - w.size))
        g = gamma.detach().cpu().numpy().reshape(-1)[: self.config.num_layers]
        b = beta.detach().cpu().numpy().reshape(-1)[: self.config.num_layers]
        bias = self.problem_hamiltonian.bias_terms.detach().cpu().numpy()
        coup = self.problem_hamiltonian.weight_couplings.detach().cpu().numpy()
        mode = getattr(self.config, "execution_mode", "qiskit_statevector")
        sim_backend = str(getattr(self.config, "simulator_backend", "auto") or "auto").lower()
        if mode == "cpp_cluster_first":
            sol = solve_qubo_cpp_cluster_first(
                weights=w,
                gamma=g,
                beta=b,
                bias=bias,
                coupling=coup,
                num_qubits=self.num_qubits,
                num_layers=int(self.config.num_layers),
            )
            return torch.from_numpy(sol.astype(np.float32)).to(
                device=weights.device, dtype=weights.dtype
            )
        ibm_name = resolve_ibm_backend_name(
            getattr(self.config, "ibm_backend_name", None),
            config_quantum_backend=getattr(self.config, "engine_quantum_backend", None),
        )
        shots = int(getattr(self.config, "ibm_shots", 1024))
        out = run_z_expectations(
            mode,
            self.num_qubits,
            int(self.config.num_layers),
            w,
            g,
            b,
            bias,
            coup,
            ibm_backend_name=(ibm_name or None),
            ibm_shots=shots,
            simulator_backend=sim_backend,
            mps_max_bond_dim=getattr(self.config, "qaoa_mps_max_bond_dim", None),
        )
        t = torch.from_numpy(out.astype(np.float32)).to(device=weights.device)
        return t.to(dtype=weights.dtype)

    def _apply_problem_hamiltonian(self, weights: torch.Tensor, gamma: torch.Tensor):
        """Apply problem Hamiltonian evolution.

        Args:
            weights: Weight configuration
            gamma: Problem Hamiltonian angle
        """
        if PENNYLANE_AVAILABLE:
            # Apply Z rotations based on problem Hamiltonian
            for i in range(self.num_qubits):
                qml.RZ(gamma * weights[i] * self.problem_hamiltonian.bias_terms[i], wires=i)

            # Apply ZZ rotations for couplings
            for i in range(self.num_qubits):
                for j in range(i + 1, self.num_qubits):
                    coupling = self.problem_hamiltonian.weight_couplings[i, j]
                    qml.RZZ(gamma * coupling, wires=[i, j])

    def _apply_mixer_hamiltonian(self, beta: torch.Tensor):
        """Apply mixer Hamiltonian evolution.

        Args:
            beta: Mixer Hamiltonian angle
        """
        if PENNYLANE_AVAILABLE:
            # Apply X rotations for mixer Hamiltonian
            for i in range(self.num_qubits):
                qml.RX(beta, wires=i)

    def _classical_simulation(
        self, weights: torch.Tensor, gamma: torch.Tensor, beta: torch.Tensor
    ) -> torch.Tensor:
        """Classical simulation of QAOA circuit.

        Args:
            weights: Weight configuration
            gamma: Problem Hamiltonian angles
            beta: Mixer Hamiltonian angles

        Returns:
            Simulated measurement results
        """
        # Simplified classical simulation
        # In practice, this would be much more complex
        result = torch.zeros(self.num_qubits)

        # Apply problem Hamiltonian effect
        for i in range(self.num_qubits):
            result[i] += gamma * weights[i] * self.problem_hamiltonian.bias_terms[i]

        # Apply mixer Hamiltonian effect
        for i in range(self.num_qubits):
            result[i] += beta * torch.sin(weights[i])

        return result

    def forward(self, weights: torch.Tensor) -> torch.Tensor:
        """Execute QAOA with current angles.

        Args:
            weights: Current weight configuration

        Returns:
            QAOA output
        """
        # Update current weights
        self.current_weights.copy_(weights)
        self._warm_start_angles_if_available(weights)
        _ = self._maybe_prune_weights(weights)

        # Predict angles if using neural prediction
        if self.angle_predictor is not None:
            predicted_angles = self.angle_predictor(weights)
            gamma = predicted_angles[: self.config.num_layers]
            beta = predicted_angles[self.config.num_layers :]
        else:
            gamma = self.gamma
            beta = self.beta

        # Execute quantum circuit
        out = self.quantum_circuit(weights, gamma, beta)
        self._update_angle_cache(weights, gamma, beta)
        return out

    def optimize_angles(
        self, weights: torch.Tensor, target_function: Callable
    ) -> Dict[str, torch.Tensor]:
        """Optimize QAOA angles for given weights.

        Args:
            weights: Current weight configuration
            target_function: Target optimization function

        Returns:
            Optimized angles
        """
        # Create optimizer for angles
        optimizer = torch.optim.Adam([self.gamma, self.beta], lr=self.config.optimizer_lr)

        # Optimization loop
        for iteration in range(self.config.max_iterations):
            optimizer.zero_grad()

            # ECL / graph step
            output = self.forward(weights)

            # Compute loss
            loss = target_function(output, weights)

            # Backward pass
            loss.backward()
            optimizer.step()

            # Log progress
            if iteration % 20 == 0:
                logger.debug(f"QAOA angle optimization iteration {iteration}, loss: {loss.item()}")

        return {
            "gamma": self.gamma.detach().clone(),
            "beta": self.beta.detach().clone(),
            "final_loss": loss.item(),
        }

    def quantum_state_injection(self, weights: torch.Tensor) -> torch.Tensor:
        """Inject quantum state into classical weights.

        Args:
            weights: Current weight configuration

        Returns:
            Updated weights with quantum injection
        """
        # Execute QAOA
        qaoa_output = self.forward(weights)

        # Apply quantum injection with small learning rate
        quantum_injection_strength = 0.01
        updated_weights = weights + quantum_injection_strength * qaoa_output

        return updated_weights


class QAOATernaryOptimizer(nn.Module):
    """Complete QAOA-based ternary optimizer.

    This class integrates QAOA with the ternary quantization pipeline,
    providing quantum-enhanced optimization for ternary weights.
    """

    def __init__(
        self,
        num_qubits: int,
        config: QAOAConfig,
        base_optimizer: Optional[torch.optim.Optimizer] = None,
    ):
        """Initialize the QAOATernaryOptimizer.

        Args:
            num_qubits: Number of qubits in the system
            config: QAOA configuration
            base_optimizer: Base classical optimizer
        """
        super(QAOATernaryOptimizer, self).__init__()
        self.num_qubits = num_qubits
        self.config = config
        self.base_optimizer = base_optimizer

        # Initialize QAOA
        self.qaoa = NeuralQAOA(num_qubits, config)

        # Optimization history
        self.optimization_history = []

    def forward(self, weights: torch.Tensor) -> torch.Tensor:
        """Optimize weights using QAOA.

        Args:
            weights: Input weights

        Returns:
            Optimized weights
        """
        # Apply QAOA optimization
        optimized_weights = self.qaoa.quantum_state_injection(weights)

        # Apply ternary quantization
        optimized_weights = self._ternary_quantize(optimized_weights)

        return optimized_weights

    def _ternary_quantize(self, weights: torch.Tensor) -> torch.Tensor:
        """Apply ternary quantization to weights.

        Args:
            weights: Continuous weights

        Returns:
            Ternary quantized weights
        """
        # Simple ternary quantization
        threshold = weights.abs().mean()
        quantized = torch.round(weights / threshold)
        quantized = torch.clamp(quantized, -1.0, 1.0)

        return quantized

    def optimize_step(
        self, weights: torch.Tensor, target_function: Callable
    ) -> Dict[str, torch.Tensor]:
        """Perform one optimization step with QAOA.

        Args:
            weights: Current weights
            target_function: Target optimization function

        Returns:
            Optimization results
        """
        # Optimize QAOA angles
        angle_results = self.qaoa.optimize_angles(weights, target_function)

        # Apply quantum optimization
        optimized_weights = self.forward(weights)

        # Store results
        result = {
            "optimized_weights": optimized_weights,
            "gamma": angle_results["gamma"],
            "beta": angle_results["beta"],
            "final_loss": angle_results["final_loss"],
        }

        self.optimization_history.append(result)

        return result

    def get_optimization_summary(self) -> Dict[str, any]:
        """Get summary of optimization process.

        Returns:
            Optimization summary
        """
        if not self.optimization_history:
            return {"status": "No optimization performed yet"}

        final_loss = self.optimization_history[-1]["final_loss"]
        num_steps = len(self.optimization_history)

        return {
            "total_optimization_steps": num_steps,
            "final_loss": final_loss,
            "qaoa_layers": self.config.num_layers,
            "problem_type": self.config.problem_type,
            "quantum_backend": self.config.quantum_backend,
        }


def create_qaoa_optimizer(
    num_qubits: int,
    config: Optional[QAOAConfig] = None,
    base_optimizer: Optional[torch.optim.Optimizer] = None,
) -> QAOATernaryOptimizer:
    """Create a QAOA-based ternary optimizer.

    Args:
        num_qubits: Number of qubits in the system
        config: QAOA configuration
        base_optimizer: Base classical optimizer

    Returns:
        QAOA ternary optimizer
    """
    if config is None:
        config = QAOAConfig()

    return QAOATernaryOptimizer(num_qubits, config, base_optimizer)


def integrate_qaoa_with_ternary_network(
    model: nn.Module, config: Optional[QAOAConfig] = None
) -> Tuple[nn.Module, QAOATernaryOptimizer]:
    """Integrate QAOA optimization with a ternary network.

    Args:
        model: Ternary network model
        config: QAOA configuration

    Returns:
        Tuple of (enhanced_model, qaoa_optimizer)
    """
    # Count total parameters for QAOA
    total_params = sum(p.numel() for p in model.parameters() if p.requires_grad)
    num_qubits = min(total_params, 20)  # Limit qubits for practical reasons

    # Create QAOA optimizer
    qaoa_optimizer = create_qaoa_optimizer(num_qubits, config)

    # Create enhanced model with QAOA integration
    class EnhancedTernaryModel(nn.Module):
        def __init__(self, base_model: nn.Module, qaoa_optimizer: QAOATernaryOptimizer):
            super(EnhancedTernaryModel, self).__init__()
            self.base_model = base_model
            self.qaoa_optimizer = qaoa_optimizer

        def forward(self, x: torch.Tensor) -> torch.Tensor:
            return self.base_model(x)

        def optimize_weights(self, target_function: Callable) -> Dict[str, torch.Tensor]:
            """Optimize model weights using QAOA.

            Args:
                target_function: Target optimization function

            Returns:
                Optimization results
            """
            # Extract current weights
            weights = torch.cat(
                [p.view(-1) for p in self.base_model.parameters() if p.requires_grad]
            )

            # Optimize using QAOA
            return self.qaoa_optimizer.optimize_step(weights, target_function)

    enhanced_model = EnhancedTernaryModel(model, qaoa_optimizer)

    return enhanced_model, qaoa_optimizer


def formulate_qahr_cost_hamiltonian_spec(escalation_payload: Dict[str, Any]) -> Dict[str, Any]:
    """Classical specification for the QAHR cost Hamilton from a CGE escalation payload.

    Full device execution is backend-specific; this returns a portable struct for logging,
    tests, and cpp/wasmedge orchestrator hooks.
    """
    loop_idx = escalation_payload.get("loop_index", -1)
    thresh = escalation_payload.get("certainty_scalar_threshold")
    last_c = escalation_payload.get("last_certainty_scalar")
    return {
        "qahr_spec_version": 1,
        "problem": "edge_fog_cloud_routing",
        "loop_index": loop_idx,
        "certainty_scalar_threshold": thresh,
        "last_certainty_scalar": last_c,
        "hamiltonian_family": "Ising+QUBO_shim",
    }


def qahr_route_after_escalation(
    quantum_moe: Optional[nn.Module],
    escalation_payload: Dict[str, Any],
    hidden_states: torch.Tensor,
) -> Dict[str, Any]:
    """QAHR after CGE: Hamiltonian spec plus optional ``QAHRRouter`` tensor pass-through.

    The ``quantum_moe`` parameter name is a legacy alias for the QAHR torch module
    (formerly ``HybridQuantumMoE``).
    """
    spec = formulate_qahr_cost_hamiltonian_spec(escalation_payload)
    out: Dict[str, Any] = {"hamiltonian_spec": spec, "routed_hidden": None}
    if quantum_moe is not None and hidden_states is not None:
        with torch.no_grad():
            hs = hidden_states
            if hs.dim() == 1:
                hs = hs.unsqueeze(0)
            out["routed_hidden"] = quantum_moe(hs)
    return out
