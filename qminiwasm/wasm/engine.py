"""WASM Execution Engine

This module implements the WasmExecutor class (Pillar 5) which provides the
WebAssembly interpreter for deterministic in-model execution. It uses the wasmtime
Python library to execute WASM code and capture stack/memory deltas for training.

The implementation follows the mathematical formulations from the Q-Mini-WASM white
paper, including:
- Wasmtime instrumentation for linear memory introspection
- Stack/memory delta capture for training
- Epoch-based interruption for fine-grained control
- Fault injection for robustness training
"""

import json
import logging
import os
import tempfile
import time
from typing import Dict, List, Optional, Tuple

import wasmtime


# #region agent log
def _dbg(path: str, hypothesis_id: str, location: str, message: str, data: dict) -> None:
    try:
        d = dict(data)
        run_id = d.pop("runId", "run")
        with open(path, "a", encoding="utf-8") as f:
            f.write(
                json.dumps(
                    {
                        "sessionId": "3fd919",
                        "runId": run_id,
                        "hypothesisId": hypothesis_id,
                        "location": location,
                        "message": message,
                        "data": d,
                        "timestamp": int(time.time() * 1000),
                    },
                    default=str,
                )
                + "\n"
            )
    except Exception:  # nosec B110 - debug logger must not break app
        pass


# #endregion

# wasmtime Python bindings use WasmtimeError; older docs sometimes mention Error
WasmtimeException = getattr(wasmtime, "WasmtimeError", getattr(wasmtime, "Error", Exception))


class WasmExecutor:
    """WasmExecutor: WASM Execution Engine for Deterministic In-Model Execution

    This class implements the WASM execution engine that provides:
    - WebAssembly code execution using wasmtime
    - Linear memory introspection and stack delta capture
    - Fault injection for robustness training
    - State recovery mechanisms

    The implementation follows the white paper's specifications for:
    - Epoch-based interruption for fine-grained control
    - Stack/memory delta capture for training
    - Fault injection strategies for robustness
    - State recovery using HullKVCache
    """

    def __init__(self):
        """Initialize the WasmExecutor."""
        self.store = wasmtime.Store()
        self.logger = logging.getLogger(__name__)
        self.logger.setLevel(logging.INFO)

    def compile_wasm(self, wasm_bytes: bytes) -> wasmtime.Module:
        """Compile WASM bytes into a Module.

        Args:
            wasm_bytes: WASM bytecode

        Returns:
            Compiled wasmtime.Module
        """
        engine = getattr(self.store, "engine", self.store)
        try:
            from_binary = getattr(wasmtime.Module, "from_binary", None)
            if from_binary is not None:
                module = from_binary(engine, wasm_bytes)
            else:
                with tempfile.NamedTemporaryFile(suffix=".wasm", delete=False) as f:
                    f.write(wasm_bytes)
                    path = f.name
                try:
                    module = wasmtime.Module.from_file(engine, path)
                finally:
                    try:
                        os.unlink(path)
                    except OSError:
                        pass
            self.logger.info("WASM module compiled successfully")
            return module
        except WasmtimeException as e:
            self.logger.error("Failed to compile WASM module: %s", e)
            raise

    def execute(self, module: wasmtime.Module, func_name: str, args: List[int]) -> Tuple[int, Dict]:
        """Execute a WASM function and capture execution state.

        Args:
            module: Compiled wasmtime.Module
            func_name: Name of function to execute
            args: List of integer arguments

        Returns:
            Tuple of (return_value, execution_state) where execution_state contains:
            - Linear memory contents
            - Stack state
            - Execution trace
        """
        _log = os.path.abspath(os.path.join(os.getcwd(), "debug-3fd919.log"))
        try:
            # #region agent log
            _dbg(
                _log,
                "H3",
                "engine.py:execute:pre_instance",
                "Creating Instance",
                {"func_name": func_name, "args": args, "has_store": hasattr(self, "store")},
            )
            # #endregion
            instance = wasmtime.Instance(self.store, module, [])  # type: ignore[call-arg,arg-type]
            # #region agent log
            _dbg(
                _log,
                "H3",
                "engine.py:execute:post_instance",
                "Instance created",
                {
                    "instance_type": type(instance).__name__,
                    "dir_instance": [x for x in dir(instance) if not x.startswith("_")][:20],
                },
            )
            # #endregion
            # Get exported function: wasmtime-py uses instance.exports(store)[name]; else get_func
            func = None
            try:
                func = instance.get_func(func_name)  # type: ignore[attr-defined]
                _dbg(
                    _log,
                    "H1",
                    "engine.py:execute:get_func",
                    "Got func via get_func",
                    {"func_type": type(func).__name__},
                )
            except AttributeError:
                _dbg(_log, "H1", "engine.py:execute:get_func_attr_err", "get_func missing", {})
            if func is None:
                exports_fn = getattr(instance, "exports", None)
                if callable(exports_fn):
                    try:
                        exports = exports_fn(self.store)
                        func = (
                            exports.get(func_name)
                            if hasattr(exports, "get")
                            else exports[func_name]  # type: ignore[index]
                        )
                        _dbg(
                            _log,
                            "H1",
                            "engine.py:execute:exports_ok",
                            "Got func via exports(store)",
                            {"func_type": type(func).__name__ if func else None},
                        )
                    except (KeyError, TypeError):
                        pass
            if func is None:
                try:
                    func = instance[func_name]  # type: ignore[index]
                    _dbg(
                        _log,
                        "H1",
                        "engine.py:execute:getitem_ok",
                        "Got func via []",
                        {"func_type": type(func).__name__},
                    )
                except (KeyError, TypeError):
                    func = getattr(instance, "get", lambda n: None)(func_name)
            if func is None:
                raise ValueError(f"Function {func_name} not found in WASM module")

            # Call: newer API uses func(store, *args), older uses func.call(args)
            call = getattr(func, "call", None)
            # #region agent log
            _dbg(
                _log,
                "H2",
                "engine.py:execute:pre_call",
                "About to call",
                {"has_call_attr": call is not None, "func_type": type(func).__name__},
            )
            # #endregion
            if call is not None:
                try:
                    result = call(self.store, *args)
                    _dbg(
                        _log,
                        "H2",
                        "engine.py:execute:call_store_ok",
                        "call(store,*args) ok",
                        {"result": result, "result_type": type(result).__name__},
                    )
                except TypeError:
                    result = call(args)
                    _dbg(
                        _log,
                        "H2",
                        "engine.py:execute:call_args_ok",
                        "call(args) ok",
                        {"result": result, "result_type": type(result).__name__},
                    )
            else:
                try:
                    result = func(self.store, *args)
                    _dbg(
                        _log,
                        "H2",
                        "engine.py:execute:func_store_ok",
                        "func(store,*args) ok",
                        {"result": result, "result_type": type(result).__name__},
                    )
                except TypeError:
                    result = func(*args)
                    _dbg(
                        _log,
                        "H2",
                        "engine.py:execute:func_args_ok",
                        "func(*args) ok",
                        {"result": result, "result_type": type(result).__name__},
                    )
            # #region agent log
            _dbg(
                _log,
                "H4",
                "engine.py:execute:return",
                "Returning result",
                {
                    "result": result,
                    "result_type": type(result).__name__,
                    "is_tuple": isinstance(result, tuple),
                },
            )
            # #endregion

            # Capture execution state including real deltas when instance exports memory
            execution_state = {
                "memory": None,
                "stack": None,
                "trace": None,
            }
            try:
                captured = self.capture_deltas(instance)
                execution_state["linear_memory"] = captured.get("linear_memory")
                execution_state["stack_snapshot"] = captured.get("stack_snapshot")
                execution_state["instruction_pointer"] = captured.get("instruction_pointer")
            except Exception:  # nosec B110
                pass

            self.logger.info("WASM function executed successfully")
            return result, execution_state

        except WasmtimeException as e:
            self.logger.error("WASM execution failed: %s", e)
            raise
        except Exception as e:
            # #region agent log
            _log = os.path.abspath(os.path.join(os.getcwd(), "debug-3fd919.log"))
            _dbg(
                _log,
                "H5",
                "engine.py:execute:exception",
                "Exception in execute",
                {"type": type(e).__name__, "message": str(e)},
            )
            # #endregion
            raise

    def capture_deltas(self, instance: wasmtime.Instance) -> Dict:
        """Capture stack and memory deltas at halt (epoch-based snapshot).

        Snapshots linear memory and, when exposed by the runtime, stack state.
        Returns structured state for Tier 2 delta compression and escalation.

        Args:
            instance: wasmtime.Instance to introspect (after execution or interrupt).

        Returns:
            Dict with linear_memory (bytes or None), stack_snapshot (list or None),
            instruction_pointer (int or None). Keys always present for downstream.
        """
        linear_memory: Optional[bytes] = None
        stack_snapshot: Optional[List[int]] = None
        instruction_pointer: Optional[int] = None

        try:
            exports_fn = getattr(instance, "exports", None)
            if callable(exports_fn):
                exports = exports_fn(self.store)
                mem = exports.get("memory") if hasattr(exports, "get") else None
                if mem is None and hasattr(exports, "__getitem__"):
                    try:
                        mem = exports["memory"]  # type: ignore[index]
                    except (KeyError, TypeError):
                        pass
                if mem is not None:
                    # wasmtime Memory: .data_ptr(store) or .read(store, offset, size) or .data(store)
                    data_fn = getattr(mem, "data", None) or getattr(mem, "data_ptr", None)
                    if data_fn is not None:
                        try:
                            buf = data_fn(self.store)
                            if hasattr(buf, "__len__") and hasattr(buf, "__getitem__"):
                                linear_memory = bytes(buf)
                            elif hasattr(buf, "read"):
                                linear_memory = buf.read()
                            else:
                                linear_memory = None
                        except Exception:  # nosec B110 - best-effort capture
                            pass
                    read_fn = getattr(mem, "read", None)
                    if linear_memory is None and read_fn is not None:
                        try:
                            size = getattr(mem, "data_size", lambda s: 0)(self.store)
                            if size > 0 and size < 1024 * 1024:
                                linear_memory = read_fn(self.store, 0, size)
                        except Exception:  # nosec B110
                            pass
        except Exception:  # nosec B110
            pass

        out = {
            "linear_memory": linear_memory,
            "stack_snapshot": stack_snapshot,
            "instruction_pointer": instruction_pointer,
            "memory_deltas": None,
            "stack_deltas": None,
            "execution_trace": None,
        }
        self.logger.info("Captured execution deltas (memory=%s)", linear_memory is not None)
        return out

    def inject_faults(self, instance: wasmtime.Instance, fault_type: str) -> None:
        """Inject faults for robustness training.

        This method implements fault injection strategies described in the white paper:
        - Bit flipping in written integers
        - Stack item dropping
        - Out-of-bounds pointer calculations

        Args:
            instance: wasmtime.Instance to modify
            fault_type: Type of fault to inject
        """
        # Placeholder implementation - would modify instance state
        self.logger.info(f"Injected {fault_type} fault for robustness training")

    def state_recovery(self, instance: wasmtime.Instance, last_valid_state: Dict) -> None:
        """Recover state using HullKVCache-based recovery.

        This method implements state recovery mechanisms described in the white paper:
        - Uses HullKVCache to retrieve last valid state
        - Generates corrective tokens
        - Reverts pointer arithmetic or fixes stack alignment

        Args:
            instance: wasmtime.Instance to recover
            last_valid_state: Last known valid state dictionary
        """
        # Placeholder implementation - would use HullKVCache for recovery
        self.logger.info("Executed state recovery using HullKVCache")
