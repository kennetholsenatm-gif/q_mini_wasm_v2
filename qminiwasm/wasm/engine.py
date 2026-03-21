"""WASM Engine for Q-Mini-WASM

This module provides the core WASM compilation and execution engine for the
Q-Mini-WASM architecture. It handles:
- C source compilation to WASM modules
- WASM module instantiation and execution
- Stack and memory state capture
- Error handling and validation
"""

import logging
import subprocess
import tempfile
import os
import wasmtime
from typing import Any, Dict, List, Optional, Tuple
import torch

logger = logging.getLogger(__name__)


class WasmEngine:
    """WASM Engine for compiling and executing WASM modules"""

    def __init__(self, use_mock: bool = False):
        """Initialize the WASM engine

        Args:
            use_mock: If True, use mock implementation instead of actual WASM compilation
        """
        self.use_mock = use_mock
        self.logger = logging.getLogger(__name__)
        self._module_cache: Dict[str, Optional[Any]] = {}
        # Keep store for last compile_wasm so Instance can use same engine (no cross-Engine)
        self._compiled_store: Optional[Any] = None
        self._compiled_module: Optional[Any] = None

        if not use_mock:
            try:
                # Test WASM compilation capability
                self._test_wasm_compilation()
                self.logger.info("WASM engine initialized with actual compilation")
            except Exception as e:
                self.logger.warning(
                    "WASM compilation not available: %s. Using mock implementation.", str(e)
                )
                self.use_mock = True

    def _test_wasm_compilation(self):
        """Test WASM compilation capability"""
        test_c_code = """
        int add(int a, int b) {
            return a + b;
        }
        """
        with tempfile.NamedTemporaryFile(suffix=".c", delete=False) as c_file:
            c_file.write(test_c_code.encode())
            c_file_path = c_file.name

        try:
            wasm_module = self.compile_c_to_wasm(c_file_path)
            os.unlink(c_file_path)
            return wasm_module is not None
        except Exception:
            if os.path.exists(c_file_path):
                os.unlink(c_file_path)
            raise

    def compile_c_to_wasm(self, c_file_path: str) -> Optional[wasmtime.Module]:
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
                compile_cmd = [
                    "clang",
                    "-target",
                    "wasm32",
                    "-nostdlib",
                    "-Wl,--no-entry",
                    "-Wl,--export-all",
                    "-o",
                    wasm_file,
                    c_file_path,
                ]

                result = subprocess.run(compile_cmd, capture_output=True, text=True)

                if result.returncode != 0:
                    self.logger.error("WASM compilation failed: %s", result.stderr)
                    return None

                # Load compiled WASM module
                try:
                    with open(wasm_file, "rb") as f:
                        wasm_bytes = f.read()
                    store = wasmtime.Store()
                    module = wasmtime.Module(store.engine, wasm_bytes)  # type: ignore[attr-defined]
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
            store = wasmtime.Store()
            module = wasmtime.Module(store.engine, wasm_code)  # type: ignore[attr-defined]
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
        output, hidden_state, target_state = self.execute_wasm(module, func_name, args)
        execution_state: Dict[str, Any] = {
            "hidden_state": hidden_state,
            "target_state": target_state,
        }
        return output, execution_state

    def execute_wasm(
        self, module: Optional[wasmtime.Module], func_name: str, args: List[int]
    ) -> Tuple[int, Optional[torch.Tensor], Optional[torch.Tensor]]:
        """Execute WASM function and capture state

        Args:
            module: WASM module to execute
            func_name: Name of the function to execute
            args: Arguments to pass to the function

        Returns:
            Tuple of (output, hidden_state, target_state)
        """
        if self.use_mock or module is None:
            return self._execute_mock(func_name, args)
        if self._compiled_store is None or self._compiled_module is not module:
            self.logger.error("Module was not compiled by this engine; use compile_wasm first.")
            return 0, None, None
        store = self._compiled_store
        try:
            instance = wasmtime.Instance(store, module, [])
            func = instance.exports(store)[func_name]  # type: ignore[index]
            # wasmtime-py: call with (store, *args); export is a Func at runtime
            result = func(store, *args)  # type: ignore[operator]
            # Unwrap single-value tuple if needed (wasmtime can return (value,))
            raw = result[0] if isinstance(result, tuple) and len(result) == 1 else result
            output = int(raw) if raw is not None else 0  # type: ignore[arg-type]
            hidden_state = torch.tensor([args[0], output, len(args)], dtype=torch.float32)
            target_state = torch.tensor([args[0], output + 1, len(args) + 1], dtype=torch.float32)

            return output, hidden_state, target_state

        except Exception as e:
            self.logger.error("WASM execution error: %s", str(e))
            return 0, None, None

    def _execute_mock(
        self, func_name: str, args: List[int]
    ) -> Tuple[int, Optional[torch.Tensor], Optional[torch.Tensor]]:
        """Mock WASM execution for testing/development

        Args:
            func_name: Name of the function to simulate
            args: Arguments to pass to the function

        Returns:
            Tuple of (output, hidden_state, target_state)
        """
        # Simulate different algorithms with distinct behaviors
        if func_name == "hash":
            output = sum(args) * 2654435761 % 1000000
            hidden = torch.tensor([args[0], output, len(args)], dtype=torch.float32)
            target = torch.tensor([args[0], output + 1, len(args) + 1], dtype=torch.float32)

        elif func_name == "encrypt":
            key = 12345
            output = args[0] ^ key
            hidden = torch.tensor([args[0], key, output], dtype=torch.float32)
            target = torch.tensor([args[0], key, output ^ 0xFF], dtype=torch.float32)

        elif func_name == "network":
            output = (args[0] + 1) % 256
            hidden = torch.tensor([args[0], output, 0], dtype=torch.float32)
            target = torch.tensor([args[0], output, 1], dtype=torch.float32)

        elif func_name == "routing":
            output = args[0] + args[1]
            hidden = torch.tensor([args[0], args[1], output], dtype=torch.float32)
            target = torch.tensor([args[0], args[1], output + 1], dtype=torch.float32)

        elif func_name == "consensus":
            output = (args[0] + args[1]) // 2
            hidden = torch.tensor([args[0], args[1], output], dtype=torch.float32)
            target = torch.tensor([args[0], args[1], (output + 1) // 2], dtype=torch.float32)

        else:
            output = sum(args) % 1000
            hidden = torch.tensor([args[0], output, len(args)], dtype=torch.float32)
            target = torch.tensor([args[0], output + 1, len(args) + 1], dtype=torch.float32)

        return output, hidden, target


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

        try:
            # Write C code to temporary file
            with tempfile.NamedTemporaryFile(suffix=".c", delete=False) as c_file:
                c_file.write(c_code.encode())
                c_file_path = c_file.name

            # Compile to WASM
            module = self.engine.compile_c_to_wasm(c_file_path)

            # Clean up temporary file
            os.unlink(c_file_path)

            if module:
                self.engine._module_cache[algorithm_name] = module
                self._source_cache[algorithm_name] = c_code
                self.logger.info("Compiled %s to WASM successfully", algorithm_name)

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
    uint32_t simple_hash(uint32_t input) {
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
    uint32_t process_packet(uint32_t packet) {
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
