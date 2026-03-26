"""Layout pilots: ``qminiwasm.wasm_host`` canonical package and ``qminiwasm.tpem`` surface."""

from __future__ import annotations


def test_wasm_host_exports_match_tpem_package_surface():
    import qminiwasm.tpem as tp
    import qminiwasm.tpem.trainable_tpem as tpt

    assert tp.save_trainable_tpem_artifact is tpt.save_trainable_tpem_artifact
    assert tp.TRAINABLE_TPEM_FORMAT_VERSION == tpt.TRAINABLE_TPEM_FORMAT_VERSION


def test_wasm_host_tpem_bundle_public_api():
    import qminiwasm.wasm_host.tpem_bundle as wh

    assert callable(wh.read_tpem_bundle)
    assert callable(wh.verify_tpem_bundle)
