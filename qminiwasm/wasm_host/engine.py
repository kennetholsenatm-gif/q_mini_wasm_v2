"""WASM Engine for Q-Mini-WASM

This module provides the core WASM compilation and execution engine for the
Q-Mini-WASM architecture. Static ternary weights loaded into the guest are a
contiguous **TPEM** byte array (see :mod:`qminiwasm.wasm_host.trit_pack` for the
5-trits-per-byte layout). It handles:

- C source compilation to WASM modules
- WASM module instantiation and execution
- Stack and linear memory state capture for **WLES** / escalation payloads
- Error handling and validation
"""

import logging
import os
import struct
import subprocess
import tempfile
import ctypes
import hashlib
import time
import shutil
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

import torch
import wasmtime

from .memory_encode import D_MODEL, encode_linear_memory
from ..native_bridge import load_native_lib, native_capabilities
from qminiwasm.runtime_modes import strict_native_enabled
from .wasi_link import build_clang_wasm_compile_command, instantiate_wasmtime_module

# Wasmtime Store max linear memory (bytes): ~1e6 rows × d_model (encode_linear_memory width).
# Override: TOML [wasm] store_memory_limit_mb or env QMW_WASM_STORE_MEMORY_LIMIT_*.
DEFAULT_WASM_STORE_MEMORY_LIMIT_BYTES = 1_000_000 * D_MODEL
# Wasmtime default instance cap is ~10k if only memory_size is set; mesh ingest may exceed it.
# Override: [wasm] store_instance_limit / store_memories_limit in training TOML.
DEFAULT_WASM_STORE_INSTANCE_LIMIT = 16_777_216
DEFAULT_WASM_STORE_MEMORIES_LIMIT = 16_777_216

logger = logging.getLogger(__name__)
try:
    import psutil
except ImportError:
    psutil = None

# Mesh curriculum: pipeline algorithm key -> exported WASM function name (clang --export-all).
MESH_EXPORT_NAMES: Dict[str, str] = {
    "hash": "simple_hash",
    "encrypt": "simple_encrypt",
    "network": "process_packet",
    "routing": "update_routing",
    "consensus": "consensus_vote",
}


@dataclass(frozen=True)
class WasmRuntimeConfig:
    """Wasmtime store limits and fallback behavior (from EngineConfig / training TOML).

    ``store_memory_limit_bytes`` may be raised via env in :meth:`EngineConfig.wasm_runtime_kwargs`.
    """

    store_memory_limit_bytes: int = DEFAULT_WASM_STORE_MEMORY_LIMIT_BYTES
    fallback_policy: str = "mock"
    backend: str = "wasmtime"
    force_mock: bool = False
    store_instance_limit: Optional[int] = None
    store_memories_limit: Optional[int] = None
    use_memory64: bool = False
    memory64_max_mb: Optional[float] = None
    enclave_tier: Optional[str] = None


class WasmEngine:
    """WASM Engine for compiling and executing WASM modules"""

    _successful_init_log_count = 0

    def __init__(self, use_mock: bool = False, runtime: Optional[WasmRuntimeConfig] = None):
        """Initialize the WASM engine

        Args:
            use_mock: If True, use mock implementation instead of actual WASM compilation
            runtime: Wasmtime limits and fallback policy (defaults in :class:`WasmRuntimeConfig`).
        """
        cfg = runtime or WasmRuntimeConfig()
        fp = (cfg.fallback_policy or "mock").strip().lower()
        if fp in ("error", "fail", "strict"):
            fp = "error"
        else:
            fp = "mock"
        self.use_mock = bool(use_mock or cfg.force_mock)
        self.logger = logging.getLogger(__name__)
        self._fallback_policy = fp
        self._backend = (cfg.backend or "wasmtime").strip().lower()
        self._enclave_tier = (cfg.enclave_tier or "").strip().lower() or None
        self._use_memory64 = bool(cfg.use_memory64)
        self._memory64_max_mb = cfg.memory64_max_mb
        self._store_memory_limit_bytes = int(cfg.store_memory_limit_bytes)
        self._store_instance_limit = cfg.store_instance_limit
        self._store_memories_limit = cfg.store_memories_limit
        self._engine = self._build_wasmtime_engine()
        self._module_cache: Dict[str, Optional[Any]] = {}
        # wasmtime.Module must be instantiated with the Store that created it.
        self._module_exec_cache: Dict[int, Tuple[Any, Any]] = {}
        # Keep store for last compile_wasm so Instance can use same engine (no cross-Engine)
        self._compiled_store: Optional[Any] = None
        self._compiled_module: Optional[Any] = None
        # One Instance per (store id, module id); mesh may reuse the same module many times.
        self._instance_cache: Dict[Tuple[int, int], Any] = {}
        self._last_execution_origin: str = "unknown"
        self._last_native_fallback_reason: str = ""

        if cfg.force_mock:
            self.logger.warning("WASM runtime config force_mock enabled; using mock WASM engine.")
        if not cfg.force_mock:
            self._log_runtime_memory_settings()
        if not self.use_mock:
            try:
                # Test WASM compilation capability
                self._test_wasm_compilation()
                WasmEngine._successful_init_log_count += 1
                if WasmEngine._successful_init_log_count == 1:
                    self.logger.info("WASM engine initialized with actual compilation")
                else:
                    self.logger.debug(
                        "WASM engine initialized with actual compilation (instance %s)",
                        WasmEngine._successful_init_log_count,
                    )
            except Exception as e:
                if self._fallback_policy == "error":
                    self.logger.error(
                        "WASM runtime initialization failed and fallback policy is 'error': %s",
                        str(e),
                    )
                    raise
                self.logger.warning(
                    "WASM compilation not available: %s. Using mock implementation.", str(e)
                )
                self.use_mock = True

    def _build_wasmtime_engine(self) -> wasmtime.Engine:
        cfg = wasmtime.Config()
        # Keep explicit defaults in one place for future per-runtime tuning.
        try:
            cfg.debug_info = False
        except Exception as exc:
            self.logger.debug("wasmtime.Config.debug_info not set (%s)", exc)
        return wasmtime.Engine(cfg)

    def _new_store(self) -> wasmtime.Store:
        store = wasmtime.Store(self._engine)
        try:
            inst_cap = (
                int(self._store_instance_limit)
                if self._store_instance_limit is not None
                else DEFAULT_WASM_STORE_INSTANCE_LIMIT
            )
            mem_cap = (
                int(self._store_memories_limit)
                if self._store_memories_limit is not None
                else DEFAULT_WASM_STORE_MEMORIES_LIMIT
            )
            store.set_limits(
                memory_size=int(self._store_memory_limit_bytes),
                instances=max(1, inst_cap),
                memories=max(1, mem_cap),
            )
        except Exception as e:
            self.logger.warning("Failed to apply wasmtime Store limits (%s). Continuing.", e)
        return store

    def _log_runtime_memory_settings(self) -> None:
        inst_cap = (
            int(self._store_instance_limit)
            if self._store_instance_limit is not None
            else DEFAULT_WASM_STORE_INSTANCE_LIMIT
        )
        mem_cap = (
            int(self._store_memories_limit)
            if self._store_memories_limit is not None
            else DEFAULT_WASM_STORE_MEMORIES_LIMIT
        )
        self.logger.info(
            "WASM runtime backend=%s memory=%s bytes instances=%s memories=%s fallback_policy=%s "
            "memory64=%s memory64_max_mb=%s enclave_tier=%s",
            self._backend,
            int(self._store_memory_limit_bytes),
            inst_cap,
            mem_cap,
            self._fallback_policy,
            self._use_memory64,
            self._memory64_max_mb,
            self._enclave_tier,
        )

    def _test_wasm_compilation(self):
        """Verify wasmtime can load and instantiate a tiny module (no clang required)."""
        wat = """
        (module
          (func $add (param i32 i32) (result i32)
            local.get 0
            local.get 1
            i32.add)
          (export "add" (func $add)))
        """
        try:
            wasm_bytes = wasmtime.wat2wasm(wat)
            store = self._new_store()
            module = wasmtime.Module(store.engine, wasm_bytes)  # type: ignore[attr-defined]
            instance = wasmtime.Instance(store, module, [])
            add_fn = instance.exports(store)["add"]  # type: ignore[index]
            _ = add_fn(store, 1, 2)
            return True
        except Exception:
            raise

    def _maybe_runtime_wasm_opt(self, wasm_file: str) -> None:
        enabled = os.environ.get("QMINIWASM_WASM_RUNTIME_WASM_OPT", "0").strip().lower() in {
            "1",
            "true",
            "yes",
            "on",
        }
        if not enabled:
            return
        wasm_opt_bin = shutil.which("wasm-opt")
        if not wasm_opt_bin:
            self.logger.debug("runtime wasm-opt requested but tool not on PATH; skipping")
            return
        level = os.environ.get("QMINIWASM_WASM_RUNTIME_WASM_OPT_LEVEL", "O2").strip()
        flag = "-" + level
        with tempfile.TemporaryDirectory(prefix="qmw_runtime_wasm_opt_") as td:
            tmp_out = os.path.join(td, "optimized.wasm")
            try:
                subprocess.run(
                    [wasm_opt_bin, flag, wasm_file, "-o", tmp_out],
                    check=True,
                    capture_output=True,
                    text=True,
                )
                with open(tmp_out, "rb") as rf:
                    optimized = rf.read()
                with open(wasm_file, "wb") as wf:
                    wf.write(optimized)
            except Exception as e:
                self.logger.debug("runtime wasm-opt failed (%s); keeping clang output", e)

    def compile_c_to_wasm(
        self, c_file_path: str, export_names: Optional[List[str]] = None
    ) -> Optional[wasmtime.Module]:
        """Compile C source file to WASM module

        Args:
            c_file_path: Path to the C source file

        Returns:
            WASM module if compilation succeeds, None otherwise
        """
        if self.use_mock:
            self.logger.info("Using mock WASM compilation")
            return None

        try:
            # Create temporary directory for compilation
            with tempfile.TemporaryDirectory() as tmpdir:
                # Compile C to WASM using clang
                wasm_file = os.path.join(tmpdir, "output.wasm")
                try:
                    compile_cmd = build_clang_wasm_compile_command(
                        c_file_path,
                        wasm_file,
                        export_names=export_names,
                    )
                except ValueError as e:
                    self.logger.error("%s", e)
                    return None

                t0 = time.perf_counter()
                result = subprocess.run(compile_cmd, capture_output=True, text=True)
                elapsed_ms = (time.perf_counter() - t0) * 1000.0

                if result.returncode != 0:
                    self.logger.error("WASM compilation failed: %s", result.stderr)
                    return None
                self._maybe_runtime_wasm_opt(wasm_file)

                # Load compiled WASM module
                try:
                    with open(wasm_file, "rb") as f:
                        wasm_bytes = f.read()
                    self.logger.info(
                        "qmw_wasm_compile clang_ms=%.2f wasm_bytes=%d link_mode=%s "
                        "profile=%s export_mode=%s exports=%s",
                        elapsed_ms,
                        len(wasm_bytes),
                        os.environ.get("QMINIWASM_WASM_C_LINK", "bare"),
                        os.environ.get("QMINIWASM_WASM_CLANG_PROFILE", "compat"),
                        os.environ.get("QMINIWASM_WASM_EXPORT_MODE", "all"),
                        ",".join(export_names or []),
                    )
                    store = self._new_store()
                    module = wasmtime.Module(store.engine, wasm_bytes)  # type: ignore[attr-defined]
                    self._module_exec_cache[id(module)] = (store, module)
                    return module
                except Exception as e:
                    self.logger.error("Failed to load WASM module: %s", str(e))
                    return None

        except FileNotFoundError:
            self.logger.error(
                "clang compiler not found. Please install clang for WASM compilation."
            )
            return None
        except Exception as e:
            self.logger.error("WASM compilation error: %s", str(e))
            return None

    def compile_wasm(self, wasm_code: bytes) -> Optional[Any]:
        """Load WASM binary into a Module (model/execute_wasm interface).

        Stores the Store used so execute_wasm can instantiate with the same
        engine (wasmtime does not support cross-Engine instantiation).

        Args:
            wasm_code: WASM binary bytes (e.g. from wasmtime.wat2wasm).

        Returns:
            wasmtime.Module or None if use_mock or load fails.
        """
        if self.use_mock:
            return None
        try:
            store = self._new_store()
            module = wasmtime.Module(store.engine, wasm_code)  # type: ignore[attr-defined]
            self._module_exec_cache[id(module)] = (store, module)
            self._compiled_store = store
            self._compiled_module = module
            return module
        except Exception as e:
            self.logger.error("Failed to load WASM module: %s", str(e))
            raise

    def execute(
        self,
        module: Optional[Any],
        func_name: str,
        args: List[int],
    ) -> Tuple[Any, Dict[str, Any]]:
        """Execute WASM function and return (result, execution_state) for model interface.

        Args:
            module: Module from compile_wasm (or None for mock).
            func_name: Export name of the function.
            args: Integer arguments.

        Returns:
            (result, execution_state) with execution_state containing
            hidden_state and target_state.
        """
        output, hidden_state, target_state, pre_mem, post_mem = self.execute_wasm(
            module, func_name, args
        )
        execution_state: Dict[str, Any] = {
            "hidden_state": hidden_state,
            "target_state": target_state,
            "wasm_memory_pre": pre_mem,
            "wasm_memory_post": post_mem,
            "wasm_execution_origin": self._last_execution_origin,
            "wasm_backend": self._backend,
        }
        return output, execution_state

    def get_trit_kernel_instance(self) -> Optional[Any]:
        """Load prebuilt trit pack / dot / ``call_indirect`` helpers (see ``trit_wasm_runtime``)."""
        try:
            from .trit_wasm_runtime import TritKernelInstance

            return TritKernelInstance.instantiate()
        except Exception as e:
            self.logger.debug("Trit kernel module unavailable: %s", e)
            return None

    def _store_for_module(self, module: Any) -> Optional[Any]:
        pair = self._module_exec_cache.get(id(module))
        if pair is not None:
            return pair[0]
        if self._compiled_store is not None and self._compiled_module is module:
            return self._compiled_store
        return None

    @staticmethod
    def _read_linear_memory(instance: Any, store: Any) -> bytes:
        try:
            mem = instance.exports(store)["memory"]  # type: ignore[index]
        except KeyError:
            return b""
        n = mem.data_len(store)
        if n <= 0:
            return b""
        return bytes(mem.read(store, 0, n))

    def execute_wasm(
        self, module: Optional[wasmtime.Module], func_name: str, args: List[int]
    ) -> Tuple[int, Optional[torch.Tensor], Optional[torch.Tensor], bytes, bytes]:
        """Execute WASM function and capture state.

        Returns:
            (output, hidden_state, target_state, pre_memory_bytes, post_memory_bytes)
        """
        exec_impl = os.getenv("QMINIWASM_WASM_EXEC_IMPL", "auto").strip().lower()
        if exec_impl not in {"auto", "python", "native"}:
            exec_impl = "auto"

        t0 = time.perf_counter()
        out: Tuple[int, Optional[torch.Tensor], Optional[torch.Tensor], bytes, bytes]
        if self._backend == "enclave_adapter":
            native = self._execute_enclave_adapter(module, func_name, args)
            if native is not None:
                out = native
                self._emit_exec_telemetry(func_name, args, out, t0, "enclave_adapter")
                return out
        if self._backend == "wasmedge_native" or exec_impl == "native":
            native = self._execute_wasmedge_native(module, func_name, args)
            if native is not None:
                self._last_execution_origin = "real"
                out = native
                self._emit_exec_telemetry(func_name, args, out, t0, "wasmedge_native")
                return out
            if exec_impl == "native":
                msg = (
                    "QMINIWASM_WASM_EXEC_IMPL=native requested but native execution unavailable; "
                    "falling back to wasmtime."
                )
                if strict_native_enabled():
                    raise RuntimeError(msg)
                self._last_native_fallback_reason = "native_wasm_unavailable"
                self.logger.warning(msg)
        if self.use_mock or module is None:
            self._last_execution_origin = "mock"
            out = self._execute_mock(func_name, args)
            self._emit_exec_telemetry(func_name, args, out, t0, "mock")
            return out
        store = self._store_for_module(module)
        if store is None:
            self.logger.error(
                "Module store missing; compile via compile_wasm or compile_c_to_wasm."
            )
            self._last_execution_origin = "error"
            out = (0, None, None, b"", b"")
            self._emit_exec_telemetry(func_name, args, out, t0, "error")
            return out
        out = self._execute_wasmtime(store, module, func_name, args)
        self._emit_exec_telemetry(func_name, args, out, t0, "wasmtime")
        return out

    def _emit_exec_telemetry(
        self,
        func_name: str,
        args: List[int],
        out: Tuple[int, Optional[torch.Tensor], Optional[torch.Tensor], bytes, bytes],
        start: float,
        impl: str,
    ) -> None:
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        rss_mb = -1.0
        if psutil is not None:
            try:
                rss_mb = float(psutil.Process().memory_info().rss) / (1024.0 * 1024.0)
            except Exception:
                rss_mb = -1.0
        output, _h, _t, pre_mem, post_mem = out
        self.logger.info(
            "qmw_wasm_exec impl=%s backend=%s func=%s argc=%d elapsed_ms=%.3f "
            "rss_mb=%.2f output=%d pre_mem=%d post_mem=%d fallback_reason=%s",
            impl,
            self._backend,
            func_name,
            len(args),
            elapsed_ms,
            rss_mb,
            int(output),
            len(pre_mem),
            len(post_mem),
            self._last_native_fallback_reason,
        )

    def _execute_wasmtime(
        self, store: Any, module: Any, func_name: str, args: List[int]
    ) -> Tuple[int, Optional[torch.Tensor], Optional[torch.Tensor], bytes, bytes]:
        try:
            ic_key = (id(store), id(module))
            instance = self._instance_cache.get(ic_key)
            if instance is None:
                instance = instantiate_wasmtime_module(store, module)
                self._instance_cache[ic_key] = instance
            pre_mem = self._read_linear_memory(instance, store)
            first_arg = int(args[0]) if args else 0
            hidden_state = encode_linear_memory(pre_mem, result_i32=0, first_arg=first_arg)

            func = instance.exports(store)[func_name]  # type: ignore[index]
            result = func(store, *args)  # type: ignore[operator]
            raw = result[0] if isinstance(result, tuple) and len(result) == 1 else result
            output = int(raw) if raw is not None else 0  # type: ignore[arg-type]

            post_mem = self._read_linear_memory(instance, store)
            target_state = encode_linear_memory(post_mem, result_i32=output, first_arg=first_arg)
            self._last_execution_origin = "real"
            return output, hidden_state, target_state, pre_mem, post_mem
        except Exception as e:
            msg = str(e)
            if "mmap failed to reserve" in msg or "Cannot allocate memory" in msg:
                detail = (
                    "WASM runtime memory reservation failed "
                    f"(store_memory_limit_bytes={int(self._store_memory_limit_bytes)}): {msg}"
                )
                if self._fallback_policy == "error":
                    raise RuntimeError(detail) from e
                self.logger.warning("%s; switching to mock WASM mode.", detail)
                self.use_mock = True
                self._last_execution_origin = "mock"
                return self._execute_mock(func_name, args)
            self.logger.error("WASM execution error: %s", msg)
            self._last_execution_origin = "error"
            return 0, None, None, b"", b""

    def _execute_enclave_adapter(
        self, module: Optional[Any], func_name: str, args: List[int]
    ) -> Optional[Tuple[int, Optional[torch.Tensor], Optional[torch.Tensor], bytes, bytes]]:
        enabled = os.getenv("QMINIWASM_ENCLAVE_ADAPTER", "0").strip().lower()
        if enabled not in {"1", "true", "yes", "on"}:
            self.logger.info(
                "enclave_adapter backend selected but adapter feature flag is off; "
                "falling back to wasmtime."
            )
            return None
        self.logger.warning(
            "enclave_adapter backend selected but native enclave adapter is unavailable; "
            "falling back to wasmtime."
        )
        return None

    def _execute_wasmedge_native(
        self, module: Optional[Any], func_name: str, args: List[int]
    ) -> Optional[Tuple[int, Optional[torch.Tensor], Optional[torch.Tensor], bytes, bytes]]:
        self._last_native_fallback_reason = ""
        caps = native_capabilities()
        lib = load_native_lib()
        if (
            lib is None
            or not bool(caps.get("symbols", {}).get("qmw_wasmedge_execute"))
            or not hasattr(lib, "qmw_wasmedge_execute")
        ):
            self._last_native_fallback_reason = "missing_qmw_wasmedge_execute"
            return None
        fn = lib.qmw_wasmedge_execute
        fn.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_int32),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_int32),
        ]
        fn.restype = ctypes.c_int
        wasm_bytes = b""
        if module is not None:
            try:
                wasm_bytes = bytes(module.serialize())  # type: ignore[attr-defined]
            except Exception:
                wasm_bytes = b""
        args_arr = (ctypes.c_int32 * max(1, len(args)))()
        for i, v in enumerate(args):
            args_arr[i] = int(v)
        out = ctypes.c_int32(0)
        wasm_arr = (
            (ctypes.c_uint8 * len(wasm_bytes)).from_buffer_copy(wasm_bytes) if wasm_bytes else None
        )
        rc = fn(
            ctypes.cast(wasm_arr, ctypes.POINTER(ctypes.c_uint8)) if wasm_arr is not None else None,
            ctypes.c_size_t(len(wasm_bytes)),
            func_name.encode("utf-8"),
            ctypes.cast(args_arr, ctypes.POINTER(ctypes.c_int32)),
            ctypes.c_size_t(len(args)),
            ctypes.byref(out),
        )
        if int(rc) != 0:
            self._last_native_fallback_reason = f"qmw_wasmedge_execute_rc_{int(rc)}"
            return None
        result = int(out.value)
        first_arg = args[0] if args else 0
        pre_b = struct.pack("<6I", 0, first_arg, 0, len(args), 0, 0) + b"\x00" * 512
        post_b = (
            struct.pack(
                "<6I",
                1,
                first_arg,
                0,
                len(args),
                result & 0xFFFFFFFF,
                (result ^ 0xFFFFFFFF) & 0xFFFFFFFF,
            )
            + b"\x00" * 512
        )
        hidden = encode_linear_memory(pre_b, result_i32=0, first_arg=first_arg)
        target = encode_linear_memory(post_b, result_i32=result, first_arg=first_arg)
        return result, hidden, target, pre_b, post_b

    def _execute_mock(
        self, func_name: str, args: List[int]
    ) -> Tuple[int, Optional[torch.Tensor], Optional[torch.Tensor], bytes, bytes]:
        """Mock WASM execution for testing/development

        Args:
            func_name: Name of the function to simulate
            args: Arguments to pass to the function

        Returns:
            Tuple of (output, hidden_state, target_state, pre_mem, post_mem)
        """

        def _mock_mem(phase: int, out: int) -> bytes:
            packed = struct.pack(
                "<6I",
                phase,
                args[0] if args else 0,
                args[1] if len(args) > 1 else 0,
                len(args),
                out & 0xFFFFFFFF,
                (out ^ 0xFFFFFFFF) & 0xFFFFFFFF,
            )
            return packed + b"\x00" * 512

        if func_name == "hash":
            output = sum(args) * 2654435761 % 1000000

        elif func_name == "encrypt":
            key = 12345
            output = args[0] ^ key

        elif func_name == "network":
            output = (args[0] + 1) % 256

        elif func_name == "routing":
            output = args[0] + args[1]

        elif func_name == "consensus":
            output = (args[0] + args[1]) // 2

        else:
            output = sum(args) % 1000

        fa = args[0] if args else 0
        pre_b = _mock_mem(0, 0)
        post_b = _mock_mem(1, output)
        hidden = encode_linear_memory(pre_b, result_i32=0, first_arg=fa)
        target = encode_linear_memory(post_b, result_i32=output, first_arg=fa)

        return output, hidden, target, pre_b, post_b


class WasmCompiler:
    """WASM Compiler for managing C source files and compilation"""

    def __init__(self, engine: WasmEngine):
        """Initialize the WASM compiler

        Args:
            engine: WASM engine instance
        """
        self.engine = engine
        self.logger = logging.getLogger(__name__)
        self._source_cache: Dict[str, str] = {}
        self._digest_cache: Dict[str, Optional[wasmtime.Module]] = {}

    def compile_algorithm(self, algorithm_name: str, c_code: str) -> Optional[wasmtime.Module]:
        """Compile algorithm C code to WASM module

        Args:
            algorithm_name: Name of the algorithm
            c_code: C source code as string

        Returns:
            WASM module if compilation succeeds, None otherwise
        """
        # Check if this algorithm was already compiled
        if algorithm_name in self.engine._module_cache:
            return self.engine._module_cache[algorithm_name]

        export_name = MESH_EXPORT_NAMES.get(algorithm_name, algorithm_name)
        digest_material = "\n".join(
            [
                c_code,
                os.environ.get("QMINIWASM_WASM_C_LINK", "bare"),
                os.environ.get("QMINIWASM_WASM_CLANG_PROFILE", "compat"),
                os.environ.get("QMINIWASM_WASM_EXPORT_MODE", "all"),
                export_name,
            ]
        ).encode("utf-8")
        digest = hashlib.sha256(digest_material).hexdigest()
        if digest in self._digest_cache:
            cached = self._digest_cache[digest]
            self.engine._module_cache[algorithm_name] = cached
            return cached

        try:
            # Write C code to temporary file
            with tempfile.NamedTemporaryFile(suffix=".c", delete=False) as c_file:
                c_file.write(c_code.encode())
                c_file_path = c_file.name

            # Compile to WASM
            module = self.engine.compile_c_to_wasm(c_file_path, export_names=[export_name])

            # Clean up temporary file
            os.unlink(c_file_path)

            if module:
                self.engine._module_cache[algorithm_name] = module
                self._source_cache[algorithm_name] = c_code
                self._digest_cache[digest] = module
                self.logger.info("Compiled %s to WASM successfully", algorithm_name)
            else:
                self._digest_cache[digest] = None

            return module

        except Exception as e:
            self.logger.error("Failed to compile %s: %s", algorithm_name, str(e))
            return None

    def get_compiled_algorithms(self) -> List[str]:
        """Get list of compiled algorithms"""
        return list(self.engine._module_cache.keys())


# Predefined algorithms for mesh network operations
MESH_ALGORITHMS = {
    "hash": """
    #include <stdint.h>
    uint32_t simple_hash(uint32_t input, uint32_t unused) {
        (void)unused;
        return input * 2654435761;
    }
    """,
    "encrypt": """
    #include <stdint.h>
    uint32_t simple_encrypt(uint32_t input, uint32_t key) {
        return input ^ key;
    }
    """,
    "network": """
    #include <stdint.h>
    uint32_t process_packet(uint32_t packet, uint32_t unused) {
        (void)unused;
        return packet + 1;
    }
    """,
    "routing": """
    #include <stdint.h>
    uint32_t update_routing(uint32_t table, uint32_t entry) {
        return table + entry;
    }
    """,
    "consensus": """
    #include <stdint.h>
    uint32_t consensus_vote(uint32_t current, uint32_t vote) {
        return (current + vote) / 2;
    }
    """,
}
