"""Layout pilots: ``qminiwasm.wasm_host`` canonical, ``qminiwasm.enclave`` shim, ``qminiwasm.tpem``."""

from __future__ import annotations


def test_wasm_host_exports_match_tpem_package_surface():
    import qminiwasm.tpem as tp
    import qminiwasm.tpem.trainable_tpem as tpt

    assert tp.save_trainable_tpem_artifact is tpt.save_trainable_tpem_artifact
    assert tp.TRAINABLE_TPEM_FORMAT_VERSION == tpt.TRAINABLE_TPEM_FORMAT_VERSION


def test_enclave_engine_shim_aliases_wasm_host():
    from qminiwasm.enclave.engine import WasmRuntimeConfig as WRC_shim
    from qminiwasm.wasm_host.engine import WasmRuntimeConfig as WRC_canon

    assert WRC_shim is WRC_canon


def test_enclave_tpem_bundle_module_matches_wasm_host():
    import qminiwasm.enclave.tpem_bundle as es
    import qminiwasm.wasm_host.tpem_bundle as wh

    assert es.read_tpem_bundle is wh.read_tpem_bundle
    assert es.verify_tpem_bundle is wh.verify_tpem_bundle
