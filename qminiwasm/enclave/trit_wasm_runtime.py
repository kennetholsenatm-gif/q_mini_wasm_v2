"""Load pre-authored trit kernel WAT/WASM (pack, scalar dot, ``call_indirect`` dispatch)."""

from __future__ import annotations

from pathlib import Path
from typing import Any, List, Optional, Tuple

_CORPUS = Path(__file__).resolve().parents[2] / "corpus"
_TRIT_WAT = _CORPUS / "trit_kernels.wat"


def trit_kernels_wat_source() -> str:
    return _TRIT_WAT.read_text(encoding="utf-8")


def compile_trit_kernels_wasm() -> bytes:
    import wasmtime

    return wasmtime.wat2wasm(trit_kernels_wat_source())


class TritKernelInstance:
    """Instantiated ``trit_kernels`` module with convenience callers."""

    def __init__(self, store: Any, instance: Any):
        self._store = store
        self._instance = instance
        exports = instance.exports(store)
        self._pack5 = exports["pack5_msb"]
        self._dot = exports["dot_u8_scalar"]
        self._dispatch = exports["dispatch_binop"]
        self.memory = exports["memory"]

    @classmethod
    def instantiate(cls) -> Optional["TritKernelInstance"]:
        try:
            import wasmtime

            wasm = compile_trit_kernels_wasm()
            store = wasmtime.Store()
            module = wasmtime.Module(store.engine, wasm)
            instance = wasmtime.Instance(store, module, [])
            return cls(store, instance)
        except Exception:
            return None

    def pack5_msb(self, w: List[int]) -> int:
        if len(w) != 5:
            raise ValueError("need exactly 5 ternary weights")
        args = [int(x) for x in w]
        return int(self._pack5(self._store, *args))

    def dot_u8_scalar(
        self,
        weights_u8: bytes,
        activations_i8: bytes,
        offset_per_lane: int = 1,
    ) -> int:
        """Dot using unsigned weight digits ``{0,1,2}`` and signed int8 activations.

        Per lane: ``digit * activation - offset_per_lane`` (constant ``offset_per_lane``,
        not activation-scaled). Relates to ternary ``{-1,0,1}`` via ``digit = t + 1`` when
        combined with an appropriate global correction.
        """
        n = min(len(weights_u8), len(activations_i8))
        if n == 0:
            return 0
        need = 16 + n * 2
        cur = self.memory.data_len(self._store)
        while cur < need:
            self.memory.grow(self._store, 1)
            cur = self.memory.data_len(self._store)
        w_off, a_off = 8, 8 + n
        self.memory.write(self._store, bytes(weights_u8[:n]), w_off)
        act_u8 = bytes(int(x) & 0xFF for x in activations_i8[:n])
        self.memory.write(self._store, act_u8, a_off)
        return int(self._dot(self._store, n, w_off, a_off, int(offset_per_lane)))

    def dispatch_binop(self, idx: int, a: int, b: int) -> int:
        return int(self._dispatch(self._store, int(idx) % 3, a, b))
