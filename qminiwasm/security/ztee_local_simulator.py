"""In-process simulator for Zero-Trust Ephemeral Enrollment (ZTEE) handshake steps.

Uses ``cryptography`` only (no third-party JWT library): RSA-signed compact assertions,
ephemeral X.509 host chain verification, optional ``jti`` revocation, and AES-256-GCM for
**WLES**-sized payloads. Intended for CI and local harnesses—not a production IdP/broker.
"""

from __future__ import annotations

import base64
import json
import os
import secrets
import time
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from typing import Any, Dict, List, Mapping, MutableSet, Optional, Tuple

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.x509.oid import NameOID


def _b64url_encode(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")


def _b64url_decode_segment(segment: str) -> bytes:
    padded = segment + "=" * (-len(segment) % 4)
    return base64.urlsafe_b64decode(padded.encode("ascii"))


def _int_to_b64url(value: int) -> str:
    length = (value.bit_length() + 7) // 8 or 1
    return _b64url_encode(value.to_bytes(length, "big"))


def rsa_private_to_jwk(private_key: rsa.RSAPrivateKey, kid: str = "ztee-local-kid") -> dict:
    """Single-key JWKS document fragment for local verification."""
    nums = private_key.public_key().public_numbers()
    return {
        "keys": [
            {
                "kty": "RSA",
                "kid": kid,
                "use": "sig",
                "alg": "RS256",
                "n": _int_to_b64url(nums.n),
                "e": _int_to_b64url(nums.e),
            }
        ]
    }


def sign_rs256_jwt(
    private_key: rsa.RSAPrivateKey,
    claims: Mapping[str, Any],
    *,
    header_kid: str = "ztee-local-kid",
) -> str:
    """Build a compact JWS (JWT) with RS256 over standard JSON claims."""
    header = {"alg": "RS256", "typ": "JWT", "kid": header_kid}
    header_b = _b64url_encode(
        json.dumps(header, separators=(",", ":"), sort_keys=True).encode("utf-8")
    )
    payload_b = _b64url_encode(
        json.dumps(dict(claims), separators=(",", ":"), sort_keys=True).encode("utf-8")
    )
    signing_input = f"{header_b}.{payload_b}".encode("ascii")
    signature = private_key.sign(signing_input, padding.PKCS1v15(), hashes.SHA256())
    return f"{header_b}.{payload_b}.{_b64url_encode(signature)}"


def verify_rs256_jwt(
    token: str,
    jwks: Mapping[str, Any],
    *,
    revoked_jti: MutableSet[str],
    clock_skew_seconds: int = 120,
) -> dict:
    """Validate RS256 JWT using ``jwks`` (``keys`` list); enforce ``exp`` and revocation."""
    parts = token.split(".")
    if len(parts) != 3:
        raise ValueError("invalid JWT shape")

    header = json.loads(_b64url_decode_segment(parts[0]).decode("utf-8"))
    payload = json.loads(_b64url_decode_segment(parts[1]).decode("utf-8"))
    kid = header.get("kid")
    if header.get("alg") != "RS256":
        raise ValueError("unsupported JWT alg")

    public_key: Optional[rsa.RSAPublicKey] = None
    for entry in jwks.get("keys", []):
        if entry.get("kty") != "RSA":
            continue
        if kid and entry.get("kid") != kid:
            continue
        n = int.from_bytes(_b64url_decode_segment(entry["n"]), "big")
        e = int.from_bytes(_b64url_decode_segment(entry["e"]), "big")
        public_key = rsa.RSAPublicNumbers(e, n).public_key()
        break
    if public_key is None:
        raise ValueError("no matching JWK for kid")

    signing_input = f"{parts[0]}.{parts[1]}".encode("ascii")
    sig = _b64url_decode_segment(parts[2])
    public_key.verify(sig, signing_input, padding.PKCS1v15(), hashes.SHA256())

    jti = str(payload.get("jti", ""))
    if jti and jti in revoked_jti:
        raise PermissionError("jti revoked")

    exp = payload.get("exp")
    if exp is not None:
        now = time.time()
        if now - clock_skew_seconds > float(exp):
            raise ValueError("JWT expired")

    return dict(payload)


def build_ephemeral_ca_and_host() -> Tuple[
    rsa.RSAPrivateKey, x509.Certificate, rsa.RSAPrivateKey, x509.Certificate
]:
    """Minimal CA + host EE certificate (RSA, SHA-256) for mTLS-style trust checks."""
    ca_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = issuer = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "ZTEE Local CA")])
    ca_cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(ca_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime.now(timezone.utc))
        .not_valid_after(datetime.now(timezone.utc) + timedelta(hours=1))
        .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
        .sign(ca_key, hashes.SHA256())
    )

    host_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    host_subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "ztee-edge-host")])
    host_cert = (
        x509.CertificateBuilder()
        .subject_name(host_subject)
        .issuer_name(ca_cert.subject)
        .public_key(host_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime.now(timezone.utc))
        .not_valid_after(datetime.now(timezone.utc) + timedelta(hours=1))
        .add_extension(
            x509.ExtendedKeyUsage([x509.oid.ExtendedKeyUsageOID.CLIENT_AUTH]),
            critical=False,
        )
        .sign(ca_key, hashes.SHA256())
    )
    return ca_key, ca_cert, host_key, host_cert


def verify_host_chain(host_cert: x509.Certificate, ca_cert: x509.Certificate) -> None:
    """Check issuer wiring + RSA-PKCS1v15 signature over TBSCertificate."""
    if host_cert.issuer != ca_cert.subject:
        raise ValueError("host certificate issuer does not match CA subject")
    issuer_pk = ca_cert.public_key()
    if not isinstance(issuer_pk, rsa.RSAPublicKey):
        raise TypeError("expected RSA CA public key")
    sig_hash = host_cert.signature_hash_algorithm
    if sig_hash is None:
        raise ValueError("host certificate missing signature hash algorithm")
    issuer_pk.verify(
        host_cert.signature,
        host_cert.tbs_certificate_bytes,
        padding.PKCS1v15(),
        sig_hash,
    )


def aes_gcm_encrypt_wles(plaintext: bytes, key: Optional[bytes] = None) -> Tuple[bytes, bytes, bytes]:
    """AES-256-GCM encrypt; returns ``(key, nonce, ciphertext_with_tag)``."""
    k = key if key is not None else os.urandom(32)
    nonce = os.urandom(12)
    aes = AESGCM(k)
    ct = aes.encrypt(nonce, plaintext, associated_data=b"wles-v1")
    return k, nonce, ct


def aes_gcm_decrypt_wles(key: bytes, nonce: bytes, ciphertext: bytes) -> bytes:
    aes = AESGCM(key)
    return aes.decrypt(nonce, ciphertext, associated_data=b"wles-v1")


@dataclass
class ZteeLocalSimulator:
    """Bundles CA/host material, IdP signing key, JWKS, and JWT revocation set."""

    ca_key: rsa.RSAPrivateKey
    ca_cert: x509.Certificate
    host_key: rsa.RSAPrivateKey
    host_cert: x509.Certificate
    idp_key: rsa.RSAPrivateKey
    jwks: Dict[str, Any]
    revoked_jti: MutableSet[str] = field(default_factory=set)

    @classmethod
    def create(cls) -> ZteeLocalSimulator:
        ca_key, ca_cert, host_key, host_cert = build_ephemeral_ca_and_host()
        idp_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        jwks = rsa_private_to_jwk(idp_key)
        return cls(
            ca_key=ca_key,
            ca_cert=ca_cert,
            host_key=host_key,
            host_cert=host_cert,
            idp_key=idp_key,
            jwks=jwks,
        )

    def revoke_jti(self, jti: str) -> None:
        self.revoked_jti.add(jti)

    def validate_cognitive_token(self, token: str) -> dict:
        """QAHR-style verifier: JWKS signature check + ``exp`` + ``jti`` revocation."""
        return verify_rs256_jwt(token, self.jwks, revoked_jti=self.revoked_jti)

    def enroll(
        self,
        *,
        tier: int = 2,
        footprint_mb: int = 2048,
        topics: Optional[List[str]] = None,
    ) -> Tuple[str, dict]:
        """Simulate bootstrap: verify host chain, mint JWT, verify it (JWKS + revocation).

        Returns ``(jwt_string, claims_dict)`` on success.
        """
        verify_host_chain(self.host_cert, self.ca_cert)

        now = datetime.now(timezone.utc)
        claims = {
            "iss": "https://ztee.local/idp",
            "sub": "enclave-local",
            "iat": int(now.timestamp()),
            "exp": int((now + timedelta(minutes=10)).timestamp()),
            "jti": secrets.token_urlsafe(18),
            "enclave_tier": tier,
            "enclave_footprint_mb": footprint_mb,
            "topics": topics or ["ingress.a", "egress.b"],
        }
        token = sign_rs256_jwt(self.idp_key, claims)
        verified = self.validate_cognitive_token(token)
        return token, verified
