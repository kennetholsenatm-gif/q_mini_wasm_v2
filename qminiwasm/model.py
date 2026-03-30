# mypy: ignore-errors
"""Main Model Interface

This module implements the QMiniWASM class (referenced in README.md) which serves as the top-level
model interface for the Q-Mini-WASM architecture. It assembles all the components into a cohesive
model that can execute WASM code and perform quantum-classical hybrid inference.

The implementation follows the white paper's specifications for:
- Model assembly and component integration
- WASM execution wrapper
- Quantum-classical hybrid inference
- Model interface methods

Training device selection follows TOML ``[hardware].accelerator`` (via :func:`get_device`); when
unset, the default is CPU unless tests pass ``prefer_xpu=True``.
"""

import logging
import math
import os
from typing import Any, Dict, List, Optional, Tuple, Union

import torch
import torch.nn as nn

from .config import DEFAULT_HIERARCHICAL_CONFIG, HierarchicalConfig
from .cognitive.edge import EdgeOutcome, default_certainty_heuristic, run_edge_cognitive_loop
from .cognitive.escalation import prepare_escalation_payload
from .fabric.qaoa_integration import QAOAConfig, qahr_route_after_escalation
from .fabric.router import HybridQuantumMoE
from .fabric.interconnect import StateMigrationInterconnect
from .layers.ternary import TernaryWASMExpert
from .layers.lota import LoRALinearSide, merge_lora_into_linear_weight
from .layers.ptqtp import PTQTPLinear
from .training.cascade_rl import CascadeRouter
from .layers.attention import TropicalAttention
from .layers.bloch_attention import BlochSphereAttention
from .wasm_host.engine import WasmEngine as WasmExecutor, WasmRuntimeConfig
from .hardware import SYCLHardware
from .hardware.device import get_device
from .data.pipeline import DataPipeline


def _qmw_env_on(name: str) -> bool:
    v = (os.environ.get(name) or "").strip().lower()
    return v in ("1", "true", "yes", "on")


def _pick_tropical_heads(d_model: int, env_heads: Optional[int]) -> int:
    if env_heads is not None and env_heads > 0 and d_model % env_heads == 0:
        return int(env_heads)
    if d_model % 8 == 0:
        return 8
    for h in (4, 2, 1):
        if d_model % h == 0:
            return h
    return 1


class QMiniWASM:
    """QMiniWASM: Top-Level Model Interface for Q-Mini-WASM Architecture

    This class implements the main model interface that assembles all components of the Q-Mini-WASM
    architecture into a cohesive model. It provides methods for:
    - WASM code execution
    - Quantum-classical hybrid inference
    - Model training and evaluation
    - Hardware acceleration interfaces

    The implementation follows the white paper's specifications for:
    - Model assembly and component integration
    - WASM execution wrapper
    - Quantum-classical hybrid inference
    - Model interface methods
    """

    def __init__(
        self,
        device=None,
        *,
        use_hybrid_adapter: bool = False,
        hybrid_adapter_hidden: int = 1024,
        tequila_deadzone: float = 0.0,
        lota_rank: int = 0,
        use_cascade_router: bool = False,
        cascade_state_dim: int = 8,
        cascade_num_actions: int = 4,
        cascade_router_hidden: int = 32,
        qaoa_execution_mode: str = "pennylane",
        num_qubits: int = 8,
        qaoa_layers: int = 3,
        ibm_qaoa_shots: int = 1024,
        quantum_backend: str = "penny_lane",
        qaoa_simulator_backend: str = "auto",
        qaoa_mps_max_bond_dim: Optional[int] = None,
        qaoa_prune_enabled: bool = False,
        qaoa_prune_threshold: float = 0.0,
        qaoa_prune_min_nodes: int = 4,
        qaoa_warm_start_cache_ttl: int = 128,
        wasm_runtime: Optional[WasmRuntimeConfig] = None,
        hierarchical_config: Optional[HierarchicalConfig] = None,
        d_model: int = 4096,
        num_ternary_blocks: int = 1,
        io_d_model: int = 4096,
        tropical_attn_per_block: bool = False,
        attention_backend: str = "tropical",
        use_gradient_checkpointing: bool = False,
    ):
        """Initialize the QMiniWASM model.

        Args:
            device: Optional torch.device; if None, uses Intel XPU when available else CPU.
            use_hybrid_adapter: If True, add a trainable residual MLP after the ternary expert
                (d_model → hidden → d_model) to increase capacity for low-MSE fits on real data.
            hybrid_adapter_hidden: Bottleneck width for the adapter (default 1024).
            tequila_deadzone: Tequila deadzone fraction for ``TernaryWASMExpert`` (0 disables).
            lota_rank: If > 0, add a LoRA side branch on the ternary expert path (LoTA-QAF).
            use_cascade_router: If True, attach :class:`CascadeRouter` (io_d_model→latent→logits)
                for cascade RL / escalation hints; trained via cascade optimizer, not main MSE Adam.
            cascade_state_dim / cascade_num_actions / cascade_router_hidden: Router shape.
            qaoa_execution_mode: ``pennylane`` (identity path), ``qiskit_statevector`` (exact),
                or ``qiskit_ibm`` (IBM Runtime Estimator; no grad through device).
            num_qubits / qaoa_layers: QAOA shape when using Qiskit modes.
            ibm_qaoa_shots: Shot budget hint for IBM Estimator (precision).
            quantum_backend: Engine / TOML logical backend; ``ibm_*`` picks IBM device if env unset.
            hierarchical_config: Tier-1 ECL/CGE/TPEM; default ``DEFAULT_HIERARCHICAL_CONFIG``.
            d_model: Internal width for ternary stack, adapter, and Qiskit router projections.
            num_ternary_blocks: Number of residual PreNorm + TernaryWASMExpert blocks.
            io_d_model: Mesh/HF encoder width (typically 4096). When different from ``d_model``,
                trainable linear stem/head map between I/O and the core.
            tropical_attn_per_block: If True, add a residual geometric attention step on the core
                (single seq_len=1 attention inside ``hybrid_inference``).
            attention_backend: ``tropical`` (max-plus hull) or ``bloch`` (fidelity on Bloch sphere);
                only used when ``tropical_attn_per_block`` is True.
            use_gradient_checkpointing:
                Recompute ternary stack activations during backward (training).
        """
        self.logger = logging.getLogger(__name__)
        self.logger.setLevel(logging.INFO)
        self.device = device if device is not None else get_device()
        self.hierarchical_config = hierarchical_config or DEFAULT_HIERARCHICAL_CONFIG
        self.d_model = max(32, int(d_model))
        self.io_d_model = max(8, int(io_d_model))
        self.num_ternary_blocks = max(1, int(num_ternary_blocks))
        self._apply_tropical_in_hybrid = bool(tropical_attn_per_block)
        self._attention_backend = str(attention_backend or "tropical").strip().lower()
        if self._attention_backend not in ("tropical", "bloch"):
            self._attention_backend = "tropical"
        self._use_gradient_checkpointing = bool(use_gradient_checkpointing)
        qaoa_cfg: Optional[QAOAConfig] = None
        mode = (qaoa_execution_mode or "pennylane").strip().lower()
        if mode in ("qiskit_statevector", "qiskit_ibm"):
            qaoa_cfg = QAOAConfig(
                num_layers=int(qaoa_layers),
                quantum_backend="ibm" if mode == "qiskit_ibm" else "default.qubit",
                use_neural_prediction=False,
                execution_mode=mode,
                ibm_shots=int(ibm_qaoa_shots),
                engine_quantum_backend=(
                    str(quantum_backend).strip() if mode == "qiskit_ibm" else None
                ),
                simulator_backend=str(qaoa_simulator_backend or "auto"),
                qaoa_mps_max_bond_dim=(
                    int(qaoa_mps_max_bond_dim) if qaoa_mps_max_bond_dim is not None else None
                ),
                qaoa_prune_enabled=bool(qaoa_prune_enabled),
                qaoa_prune_threshold=float(qaoa_prune_threshold),
                qaoa_prune_min_nodes=max(1, int(qaoa_prune_min_nodes)),
                qaoa_warm_start_cache_ttl=max(1, int(qaoa_warm_start_cache_ttl)),
            )
            self.logger.info(
                "quantum_router: QAOA via Qiskit (%s, num_qubits=%s layers=%s)",
                mode,
                int(num_qubits),
                int(qaoa_layers),
            )
        # Initialize all components
        self.quantum_router = HybridQuantumMoE(
            qaoa_config=qaoa_cfg,
            num_qubits=int(num_qubits),
            d_model=self.d_model,
        ).to(self.device)
        if self.io_d_model != self.d_model:
            self.input_stem = nn.Linear(self.io_d_model, self.d_model).to(self.device)
            self.output_head = nn.Linear(self.d_model, self.io_d_model).to(self.device)
            nn.init.kaiming_uniform_(self.input_stem.weight, a=math.sqrt(5))
            nn.init.zeros_(self.input_stem.bias)
            nn.init.kaiming_uniform_(self.output_head.weight, a=math.sqrt(5))
            nn.init.zeros_(self.output_head.bias)
        else:
            self.input_stem = nn.Identity()
            self.output_head = None
        self.block_norms = nn.ModuleList(
            [nn.LayerNorm(self.d_model) for _ in range(self.num_ternary_blocks)]
        ).to(self.device)
        self.ternary_blocks = nn.ModuleList(
            [
                TernaryWASMExpert(
                    self.d_model,
                    self.d_model,
                    tequila_deadzone=float(tequila_deadzone),
                ).to(self.device)
                for _ in range(self.num_ternary_blocks)
            ]
        )
        self.lota_branch: Optional[LoRALinearSide] = None
        if int(lota_rank) > 0:
            self.lota_branch = LoRALinearSide(self.d_model, self.d_model, int(lota_rank)).to(
                self.device
            )
            self.logger.info("LoTA-QAF LoRA branch enabled (rank=%s)", int(lota_rank))
        self._ptqtp_linear: Optional[PTQTPLinear] = None
        self._use_ptqtp_inference: bool = False
        self.hybrid_adapter: Optional[nn.Module] = None
        if use_hybrid_adapter:
            h_adapt = max(32, int(hybrid_adapter_hidden))
            self._build_hybrid_adapter(h_adapt)
            self.logger.info(
                "hybrid_adapter enabled (hidden=%s) for extra trainable capacity",
                h_adapt,
            )
        self.wasm_executor = WasmExecutor(use_mock=False, runtime=wasm_runtime)
        self.sycl_hardware = SYCLHardware()
        self.data_pipeline = DataPipeline(wasm_runtime=wasm_runtime)
        self.state_migration = StateMigrationInterconnect()
        # Edge profiling: QMW_DISABLE_TROPICAL_ATTN and QMW_TROPICAL_ATTN_HEADS (d_model % N == 0).
        heads_s = (os.environ.get("QMW_TROPICAL_ATTN_HEADS") or "").strip()
        env_nh = int(heads_s) if heads_s.isdigit() else None
        nh = _pick_tropical_heads(self.d_model, env_nh)
        self.tropical_attention = None
        self.bloch_sphere_attention = None
        if self._apply_tropical_in_hybrid and not _qmw_env_on("QMW_DISABLE_TROPICAL_ATTN"):
            if self._attention_backend == "bloch":
                self.bloch_sphere_attention = BlochSphereAttention(
                    self.d_model, num_heads=nh
                ).to(self.device)
            else:
                self.tropical_attention = TropicalAttention(self.d_model, num_heads=nh).to(
                    self.device
                )
        self.cascade_router: Optional[CascadeRouter] = None
        if bool(use_cascade_router):
            self.cascade_router = CascadeRouter(
                d_model=self.io_d_model,
                state_dim=int(cascade_state_dim),
                num_actions=int(cascade_num_actions),
                hidden=max(8, int(cascade_router_hidden)),
            ).to(self.device)
            self.logger.info(
                "cascade_router attached (state_dim=%s num_actions=%s)",
                int(cascade_state_dim),
                int(cascade_num_actions),
            )
        self.logger.info(
            "QMiniWASM model initialized on %s (d_model=%s io_d_model=%s ternary_blocks=%s)",
            self.device,
            self.d_model,
            self.io_d_model,
            self.num_ternary_blocks,
        )

    @property
    def ternary_expert(self) -> TernaryWASMExpert:
        """First ternary block (backward compatible with single-block checkpoints)."""
        return self.ternary_blocks[0]

    def ternary_latent_weights(self) -> List[torch.nn.Parameter]:
        """Continuous latent weights for all ternary blocks (e.g. T-Sign optimizer)."""
        return [b.weight for b in self.ternary_blocks]

    def _forward_ternary_stack(self, routed: torch.Tensor) -> torch.Tensor:
        y = routed
        for norm, blk in zip(self.block_norms, self.ternary_blocks):
            y = y + blk(norm(y))
        return y

    def _build_hybrid_adapter(self, hidden: int) -> None:
        """Residual branch: Linear → GELU → Linear; last layer zero-init (ternary-only start)."""
        m = nn.Sequential(
            nn.Linear(self.d_model, hidden),
            nn.GELU(),
            nn.Linear(hidden, self.d_model),
        ).to(self.device)
        nn.init.kaiming_uniform_(m[0].weight, a=math.sqrt(5))
        nn.init.zeros_(m[0].bias)
        nn.init.zeros_(m[2].weight)
        nn.init.zeros_(m[2].bias)
        self.hybrid_adapter = m

    def attach_hybrid_adapter_matching_state(self, state_dict: Dict[str, Any]) -> None:
        """If no adapter yet, allocate one matching ``state_dict`` (e.g. first TPEM load)."""
        if self.hybrid_adapter is not None:
            return
        w0 = state_dict.get("0.weight")
        if w0 is None:
            return
        hidden = int(w0.shape[0])
        self._build_hybrid_adapter(hidden)
        self.logger.info("Built hybrid_adapter (hidden=%s) to match trainable TPEM shape.", hidden)

    def trainable_hybrid_backbone_parameters(self) -> List[torch.nn.Parameter]:
        """Parameters stepped by the engine training loop (router + ternary + optional adapter)."""
        params: List[torch.nn.Parameter] = []
        params.extend(self.quantum_router.parameters())
        if isinstance(self.input_stem, nn.Linear):
            params.extend(self.input_stem.parameters())
        for blk in self.ternary_blocks:
            params.extend(blk.parameters())
        if self.output_head is not None:
            params.extend(self.output_head.parameters())
        if self.lota_branch is not None:
            params.extend(self.lota_branch.parameters())
        if self.hybrid_adapter is not None:
            params.extend(self.hybrid_adapter.parameters())
        if self.tropical_attention is not None:
            params.extend(self.tropical_attention.parameters())
        if self.bloch_sphere_attention is not None:
            params.extend(self.bloch_sphere_attention.parameters())
        return params

    def trainable_adam_parameters(
        self, exclude_ternary_weight: bool = False
    ) -> List[torch.nn.Parameter]:
        """Subset for AdamW when ternary latent is updated with :class:`TSignSGD` instead."""
        if not exclude_ternary_weight:
            return self.trainable_hybrid_backbone_parameters()
        out: List[torch.nn.Parameter] = []
        out.extend(self.quantum_router.parameters())
        if isinstance(self.input_stem, nn.Linear):
            out.extend(self.input_stem.parameters())
        for blk in self.ternary_blocks:
            if blk.bias is not None:
                out.append(blk.bias)
        if self.output_head is not None:
            out.extend(self.output_head.parameters())
        if self.lota_branch is not None:
            out.extend(self.lota_branch.parameters())
        if self.hybrid_adapter is not None:
            out.extend(self.hybrid_adapter.parameters())
        if self.tropical_attention is not None:
            out.extend(self.tropical_attention.parameters())
        if self.bloch_sphere_attention is not None:
            out.extend(self.bloch_sphere_attention.parameters())
        return out

    def cascade_logits(self, hidden: torch.Tensor) -> Optional[torch.Tensor]:
        """Return logits ``[num_actions]`` from hidden ``[d_model]`` or mean-pooled batch."""
        if self.cascade_router is None:
            return None
        if hidden.dim() == 2:
            h = hidden.mean(dim=0).detach()
        else:
            h = hidden.detach()
        s = self.cascade_router.project_hidden(h)
        return self.cascade_router(s)

    def cascade_logits_rows(self, hidden_batch: torch.Tensor) -> Optional[torch.Tensor]:
        """Per-row logits ``[B, num_actions]`` if ``cascade_router`` is set; else ``None``."""
        if self.cascade_router is None:
            return None
        if hidden_batch.dim() != 2 or hidden_batch.shape[1] != self.io_d_model:
            raise ValueError(
                f"Expected hidden [B, {self.io_d_model}], got {tuple(hidden_batch.shape)}"
            )
        cr = self.cascade_router
        s = cr.projector(hidden_batch.detach())
        return cr.body(s)

    def merge_lota_into_ternary(self) -> None:
        """Fold LoRA ``B @ A`` into ``ternary_expert.weight`` and zero ``lora_b``."""
        if self.lota_branch is None:
            return
        merge_lora_into_linear_weight(self.ternary_expert.weight, self.lota_branch)

    def enable_ptqtp_inference(self, num_planes: int = 2) -> None:
        """Replace ternary expert forward with a frozen PTQTP decomposition (eval-style)."""
        self._ptqtp_linear = PTQTPLinear(
            self.ternary_expert.weight.data.detach().clone(),
            num_planes=int(num_planes),
        ).to(self.device)
        self._use_ptqtp_inference = True
        self.logger.info("PTQTP inference enabled (%s planes)", int(num_planes))

    def disable_ptqtp_inference(self) -> None:
        self._ptqtp_linear = None
        self._use_ptqtp_inference = False

    def load_trainable_checkpoint(
        self, path: str, map_location: Optional[Union[str, torch.device]] = None
    ) -> Dict:
        """Load trainable TPEM weights (router, ternary, optional adapter).

        Args:
            path: Filesystem path to ``.pt`` trainable TPEM payload.
            map_location: Passed to ``torch.load``; defaults to ``self.device``.

        Returns:
            Serialized ``meta`` dict from the artifact (may be empty).
        """
        from qminiwasm.tpem.trainable_tpem import load_trainable_tpem_into_model

        loc = map_location if map_location is not None else self.device
        meta = load_trainable_tpem_into_model(self, path, map_location=loc)
        self.logger.info("Loaded trainable TPEM from %s", path)
        return meta

    def execute_wasm(self, wasm_code: bytes, func_name: str, args: List[int]) -> Tuple[int, Dict]:
        """Execute WASM code using the WASM execution engine.

        This method implements the execute_wasm method referenced in README.md:
        - Compiles WASM code
        - Executes specified function
        - Captures execution state
        - Returns result and execution information

        Args:
            wasm_code: WASM bytecode to execute
            func_name: Name of function to execute
            args: List of integer arguments

        Returns:
            Tuple of (return_value, execution_state) where execution_state contains:
            - Return value
            - Execution trace
            - Memory state
            - Stack state
        """
        try:
            # Compile WASM code
            module = self.wasm_executor.compile_wasm(wasm_code)

            # Execute function
            result, execution_state = self.wasm_executor.execute(module, func_name, args)

            self.logger.info(f"Executed WASM function {func_name} successfully")
            return result, execution_state

        except Exception as e:
            self.logger.error(f"WASM execution failed: {e}")
            raise

    def run_edge_inference(
        self,
        wasm_code: bytes,
        func_name: str,
        args: List[int],
        compute_certainty: Optional[Any] = None,
        config: Optional[HierarchicalConfig] = None,
    ) -> Tuple[Any, EdgeOutcome, int, Dict]:
        """Run Tier 1 edge cognitive loop: local WASM + N-loop halting + escalation.

        Runs up to N_max_loops execution blocks; after each block computes a
        certainty scalar and stops when certainty > T_conf (RESOLVED_LOCAL) or
        when N loops are done (ESCALATE_TO_CLOUD).

        Args:
            wasm_code: WASM bytecode.
            func_name: Function to execute each block.
            args: Arguments for the function.
            compute_certainty: Callable(state_dict) -> float in [0,1]. Default heuristic.
            config: Hierarchical config; uses DEFAULT_HIERARCHICAL_CONFIG if None.

        Returns:
            (result, outcome, num_loops, last_state).
        """
        module = self.wasm_executor.compile_wasm(wasm_code)
        cfg = config or self.hierarchical_config

        def execute_one_block(loop_idx: int) -> Tuple[Any, Dict]:
            result, execution_state = self.wasm_executor.execute(module, func_name, args)
            state = {"execution_state": execution_state, "result": result, "loop_idx": loop_idx}
            return result, state

        certainty_fn = (
            compute_certainty if compute_certainty is not None else default_certainty_heuristic
        )
        result, outcome, num_loops, last_state = run_edge_cognitive_loop(
            execute_one_block, certainty_fn, cfg
        )
        if outcome in (EdgeOutcome.ESCALATE_TO_CLOUD, EdgeOutcome.ESCALATE_TO_FOG):
            from .cognitive.semantic_abstraction import attach_semantic_blob_to_state

            attach_semantic_blob_to_state(last_state, device=self.device)
            last_state["escalation_payload"] = prepare_escalation_payload(last_state, cfg)
        return result, outcome, num_loops, last_state

    def run_hierarchical(
        self,
        wasm_code: bytes,
        func_name: str,
        args: List[int],
        continuation_hidden_states: Optional[torch.Tensor] = None,
        compute_certainty: Optional[Any] = None,
        config: Optional[HierarchicalConfig] = None,
    ) -> Tuple[Any, EdgeOutcome, int, Dict]:
        """Single entry point for hierarchical inference: Tier 1 -> Tier 2 -> Tier 3.

        (1) Runs Tier 1 edge cognitive loop (local WASM + N-loop + certainty).
        (2) If ESCALATE_TO_CLOUD, builds escalation payload and runs Tier 2 state
            migration (ingest deltas into HullKVCache) then Tier 3 (hybrid inference).
        (3) Returns (result, outcome, num_loops, state); state includes
            escalation_payload when outcome is ESCALATE_TO_CLOUD.

        Args:
            wasm_code: WASM bytecode for edge execution.
            func_name: Function name to execute each block.
            args: Arguments for the function.
            continuation_hidden_states: For cloud path after escalation; if None,
                a zero tensor (1, d_model) is used.
            compute_certainty: Optional certainty callable; default heuristic otherwise.
            config: Optional hierarchical config.

        Returns:
            (result, outcome, num_loops, last_state).
        """
        result, outcome, num_loops, last_state = self.run_edge_inference(
            wasm_code, func_name, args, compute_certainty=compute_certainty, config=config
        )
        if outcome in (EdgeOutcome.ESCALATE_TO_CLOUD, EdgeOutcome.ESCALATE_TO_FOG):
            payload = last_state.get("escalation_payload")
            if payload is not None and continuation_hidden_states is not None:
                self.inference_from_escalation(payload, continuation_hidden_states)
            elif payload is not None:
                dev = self.device
                dummy = torch.zeros(1, self.io_d_model, device=dev, dtype=torch.float32)
                self.inference_from_escalation(payload, dummy)
        return result, outcome, num_loops, last_state

    def inference_from_escalation(
        self,
        payload: Dict[str, Any],
        continuation_hidden_states: torch.Tensor,
    ) -> torch.Tensor:
        """Re-hydrate from Tier 2 escalation payload and resume at step N+1 (cloud).

        Ingests delta payload into tropical attention (legacy **ESI** migration path), then runs
        **Quantum-Assisted Hierarchical Routing (QAHR)** cost shaping and hybrid inference.

        Args:
            payload: From prepare_escalation_payload or last_state['escalation_payload'].
            continuation_hidden_states: Hidden states for the continuation step
                (batch_size, d_model).

        Returns:
            Output tensor from hybrid inference (batch_size, d_model).
        """
        qahr = qahr_route_after_escalation(self.quantum_router, payload, continuation_hidden_states)
        self.logger.debug("QAHR Hamiltonian spec: %s", qahr.get("hamiltonian_spec"))
        deltas = self.state_migration.accept(payload)
        if deltas and hasattr(self, "tropical_attention") and self.tropical_attention is not None:
            self.tropical_attention.ingest_deltas(deltas, device=self.device)
        return self.hybrid_inference(continuation_hidden_states)

    def hybrid_inference(self, hidden_states: torch.Tensor) -> torch.Tensor:
        """Perform quantum-classical hybrid inference.

        This method implements quantum-classical hybrid inference:
        - Uses quantum router for MoE routing
        - Applies ternary quantization for WASM experts
        - Combines results from different expert types
        - Returns final output tensor

        Args:
            hidden_states: Input tensor of shape (batch_size, d_model)

        Returns:
            Output tensor of shape (batch_size, d_model)
        """
        try:
            x = hidden_states.to(self.device)
            if x.dim() != 2 or x.shape[1] != self.io_d_model:
                raise ValueError(
                    f"Expected hidden_states [batch, {self.io_d_model}], got {tuple(x.shape)}"
                )
            h = self.input_stem(x)
            routed_output = self.quantum_router(h)

            if self._use_ptqtp_inference and self._ptqtp_linear is not None:
                ternary_output = self._ptqtp_linear(routed_output)
            elif self._use_gradient_checkpointing and self.training:
                ternary_output = torch.utils.checkpoint.checkpoint(
                    self._forward_ternary_stack,
                    routed_output,
                    use_reentrant=False,
                )
            else:
                ternary_output = self._forward_ternary_stack(routed_output)

            if self._apply_tropical_in_hybrid and not self._use_ptqtp_inference:
                t2 = ternary_output.unsqueeze(1)
                if self.bloch_sphere_attention is not None:
                    ternary_output = ternary_output + self.bloch_sphere_attention(t2).squeeze(1)
                elif self.tropical_attention is not None:
                    ternary_output = ternary_output + self.tropical_attention(t2).squeeze(1)

            if self.lota_branch is not None and not self._use_ptqtp_inference:
                ternary_output = ternary_output + self.lota_branch(routed_output)
            if self.hybrid_adapter is not None:
                ternary_output = ternary_output + self.hybrid_adapter(ternary_output)
            if self.output_head is not None:
                ternary_output = self.output_head(ternary_output)

            self.logger.debug("Completed hybrid inference")
            return ternary_output

        except Exception as e:
            self.logger.error(f"Hybrid inference failed: {e}")
            raise

    def train(self, training_data: List[Dict], epochs: int = 10) -> None:
        """Train the QMiniWASM model.

        This method implements model training:
        - Uses data pipeline for training data generation
        - Implements fault injection for robustness
        - Applies continuous pre-training curriculum
        - Updates model parameters

        Args:
            training_data: Training data samples
            epochs: Number of training epochs
        """
        try:
            # Use data pipeline for training (mesh curriculum; matches training loop defaults)
            self.data_pipeline.generate_training_data(
                algorithms=["hash", "encrypt", "network", "routing", "consensus"],
                num_samples=max(1, len(training_data)),
            )

            # Train model (placeholder implementation)
            self.logger.info(f"Training QMiniWASM for {epochs} epochs")
            # Actual training implementation would go here

        except Exception as e:
            self.logger.error(f"Training failed: {e}")
            raise

    def evaluate(self, test_data: List[Dict]) -> Dict:
        """Evaluate the QMiniWASM model.

        This method implements model evaluation:
        - Tests model performance on test data
        - Measures accuracy and other metrics
        - Returns evaluation results

        Args:
            test_data: Test data samples

        Returns:
            Dictionary of evaluation metrics
        """
        try:
            # Evaluate model (placeholder implementation)
            self.logger.info("Evaluating QMiniWASM model")
            return {"accuracy": 0.0, "loss": 0.0, "metrics": {}}

        except Exception as e:
            self.logger.error(f"Evaluation failed: {e}")
            raise
