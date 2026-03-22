//! Stub WasmEdge WASI-NN plugin — register host graphs for ternary dot / GEMV.
//!
//! Replace with real `wasmedge-sys` / plugin registration when targeting WasmEdge.

/// Placeholder export so `cargo build` produces an empty cdylib artifact.
#[no_mangle]
pub extern "C" fn qminiwasm_ternary_nn_plugin_abi_version() -> u32 {
    1
}
