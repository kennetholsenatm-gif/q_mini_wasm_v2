"""ZTEE local simulator: host X.509 chain, OIDC-style JWT + JWKS, revocation, WLES AES-GCM."""

from __future__ import annotations

import unittest


class TestZteeHandshakeSimulator(unittest.TestCase):
    def test_enrollment_success_host_chain_and_jwt(self):
        from qminiwasm.security.ztee_local_simulator import (
            ZteeLocalSimulator,
            verify_host_chain,
        )

        sim = ZteeLocalSimulator.create()
        verify_host_chain(sim.host_cert, sim.ca_cert)
        token, claims = sim.enroll(tier=2, footprint_mb=2048)
        self.assertIn("jti", claims)
        self.assertEqual(claims["enclave_tier"], 2)
        self.assertIsInstance(token, str)
        self.assertEqual(token.count("."), 2)

    def test_enrollment_fails_when_jti_revoked(self):
        from qminiwasm.security.ztee_local_simulator import ZteeLocalSimulator

        sim = ZteeLocalSimulator.create()
        token, claims = sim.enroll()
        jti = claims["jti"]
        sim.revoke_jti(jti)
        with self.assertRaises(PermissionError):
            sim.validate_cognitive_token(token)

    def test_wles_aes_gcm_round_trip(self):
        from qminiwasm.security.ztee_local_simulator import (
            aes_gcm_decrypt_wles,
            aes_gcm_encrypt_wles,
        )

        plaintext = b"wles-snapshot-bytes-\x00\xffdemo"
        key, nonce, ct = aes_gcm_encrypt_wles(plaintext)
        self.assertEqual(len(key), 32)
        self.assertEqual(len(nonce), 12)
        out = aes_gcm_decrypt_wles(key, nonce, ct)
        self.assertEqual(out, plaintext)

    def test_oidc_provider_metadata_shape(self):
        from qminiwasm.security.ztee_local_simulator import build_oidc_provider_metadata

        meta = build_oidc_provider_metadata(
            issuer="https://ztee.local/idp",
            jwks_uri="https://ztee.local/idp/jwks.json",
            token_endpoint="https://ztee.local/idp/token",
        )
        self.assertEqual(meta["issuer"], "https://ztee.local/idp")
        self.assertIn("jwks_uri", meta)
        self.assertIn("token_endpoint", meta)
        self.assertIn("RS256", meta["id_token_signing_alg_values_supported"])

    def test_aes_gcm_rejects_tampering(self):
        from qminiwasm.security.ztee_local_simulator import (
            aes_gcm_decrypt_wles,
            aes_gcm_encrypt_wles,
        )

        key, nonce, ct = aes_gcm_encrypt_wles(b"secret-state")
        bad = bytearray(ct)
        bad[0] ^= 0x01
        with self.assertRaises(Exception):
            aes_gcm_decrypt_wles(key, nonce, bytes(bad))


if __name__ == "__main__":
    unittest.main()
