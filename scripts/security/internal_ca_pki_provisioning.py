#!/usr/bin/env python3
"""
Internal X.509 Certificate Authority (CA) PKI provisioning for edge enclaves.

Automates TLS certificate request and provisioning against an **internal CA**
that exposes an IPA-compatible JSON-RPC session API (common in enterprise
identity stacks). Vendor-neutral configuration uses ``INTERNAL_CA_*`` env
vars, with legacy ``FREEIPA_*`` aliases for backward compatibility.

Features:
- Authenticates via Kerberos or username/password to the CA control plane
- Issues certificates for edge gateways, quantum-router components, and WASM enclaves
- Renewal, revocation hooks, and mTLS config artifacts
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import requests
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from requests.auth import HTTPBasicAuth
from requests_kerberos import HTTPKerberosAuth, OPTIONAL

# Configuration: prefer INTERNAL_CA_*; fall back to legacy FREEIPA_* for existing deployments.
INTERNAL_CA_SERVER = os.getenv("INTERNAL_CA_SERVER") or os.getenv(
    "FREEIPA_SERVER", "https://ipa.qminiwasm.local"
)
INTERNAL_CA_REALM = os.getenv("INTERNAL_CA_REALM") or os.getenv("FREEIPA_REALM", "QMINIWASM.LOCAL")
CERT_VALIDITY_DAYS = int(os.getenv("CERT_VALIDITY_DAYS", "365"))
CERT_KEY_SIZE = int(os.getenv("CERT_KEY_SIZE", "2048"))
CERT_ALGORITHM = hashes.SHA256()

logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


class InternalCAPKIProvisioner:
    """PKI provisioning client for an internal X.509 CA (IPA-style JSON API)."""

    def __init__(self, server_url: str = INTERNAL_CA_SERVER, realm: str = INTERNAL_CA_REALM):
        self.server_url = server_url.rstrip("/")
        self.realm = realm
        self.session = requests.Session()
        self.session.verify = True

        self.kerberos_auth = HTTPKerberosAuth(mutual_authentication=OPTIONAL)

        # IPA-compatible JSON-RPC session endpoints
        self.base_endpoint = f"{self.server_url}/ipa/session/json"
        self.certificate_endpoint = f"{self.server_url}/ipa/cert_request"

    def authenticate(self, username: Optional[str] = None, password: Optional[str] = None) -> bool:
        """Authenticate to the internal CA control plane."""
        try:
            if username and password:
                auth = HTTPBasicAuth(username, password)
                response = self.session.post(
                    f"{self.server_url}/ipa/session/login_password",
                    headers={"Referer": self.server_url},
                    auth=auth,
                    data={"user": username, "password": password},
                )
            else:
                response = self.session.post(
                    f"{self.server_url}/ipa/session/login_kerberos",
                    headers={"Referer": self.server_url},
                    auth=self.kerberos_auth,
                )

            if response.status_code == 200:
                logger.info("Successfully authenticated to internal CA")
                return True
            logger.error("Authentication failed: %s - %s", response.status_code, response.text)
            return False

        except Exception as e:
            logger.error("Authentication error: %s", e)
            return False

    def generate_certificate_request(
        self, hostname: str, service_type: str, sans: Optional[List[str]] = None
    ) -> Tuple[str, str]:
        """Generate a CSR and PEM private key for the given service."""
        try:
            private_key = rsa.generate_private_key(
                public_exponent=65537,
                key_size=CERT_KEY_SIZE,
            )

            subject = x509.Name(
                [
                    x509.NameAttribute(x509.NameOID.COUNTRY_NAME, "US"),
                    x509.NameAttribute(x509.NameOID.STATE_OR_PROVINCE_NAME, "CA"),
                    x509.NameAttribute(x509.NameOID.LOCALITY_NAME, "San Francisco"),
                    x509.NameAttribute(x509.NameOID.ORGANIZATION_NAME, "QMiniWASM"),
                    x509.NameAttribute(x509.NameOID.ORGANIZATIONAL_UNIT_NAME, service_type.title()),
                    x509.NameAttribute(x509.NameOID.COMMON_NAME, hostname),
                ]
            )

            san_list = [x509.DNSName(hostname)]
            if sans:
                for san in sans:
                    san_list.append(x509.DNSName(san))

            csr = (
                x509.CertificateSigningRequestBuilder()
                .subject_name(subject)
                .add_extension(
                    x509.SubjectAlternativeName(san_list),
                    critical=False,
                )
                .sign(private_key, CERT_ALGORITHM)
            )

            csr_pem = csr.public_bytes(serialization.Encoding.PEM).decode("utf-8")
            private_key_pem = private_key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.PKCS8,
                encryption_algorithm=serialization.NoEncryption(),
            ).decode("utf-8")

            logger.info("Generated CSR for %s", hostname)
            return csr_pem, private_key_pem

        except Exception as e:
            logger.error("Error generating CSR: %s", e)
            raise

    def request_certificate(
        self, hostname: str, service_type: str, csr: str, profile_id: str = "caIPAserviceCert"
    ) -> Dict:
        """Submit CSR to the internal CA."""
        try:
            request_data = {
                "method": "cert_request",
                "params": [
                    [csr],
                    {
                        "add": True,
                        "principal": f"HTTP/{hostname}@{self.realm}",
                        "profile_id": profile_id,
                        "version": "2.167",
                    },
                ],
            }

            headers = {"Content-Type": "application/json", "Referer": self.server_url}

            response = self.session.post(self.base_endpoint, headers=headers, json=request_data)

            if response.status_code == 200:
                result = response.json()
                if result.get("error"):
                    logger.error("Certificate request failed: %s", result["error"])
                    return {}

                logger.info("Certificate requested for %s", hostname)
                return result.get("result", {})
            logger.error("Certificate request failed: %s - %s", response.status_code, response.text)
            return {}

        except Exception as e:
            logger.error("Error requesting certificate: %s", e)
            raise

    def get_certificate(self, serial_number: int) -> Dict:
        """Fetch certificate details by serial number."""
        try:
            request_data = {"method": "cert_show", "params": [[serial_number], {}]}

            headers = {"Content-Type": "application/json", "Referer": self.server_url}

            response = self.session.post(self.base_endpoint, headers=headers, json=request_data)

            if response.status_code == 200:
                result = response.json()
                if result.get("error"):
                    logger.error("Certificate retrieval failed: %s", result["error"])
                    return {}

                logger.info("Retrieved certificate with serial %s", serial_number)
                return result.get("result", {})
            logger.error(
                "Certificate retrieval failed: %s - %s", response.status_code, response.text
            )
            return {}

        except Exception as e:
            logger.error("Error retrieving certificate: %s", e)
            raise

    def revoke_certificate(self, serial_number: int, reason: str = "unspecified") -> bool:
        """Revoke a certificate by serial number."""
        try:
            request_data = {
                "method": "cert_revoke",
                "params": [[serial_number], {"revocation_reason": reason, "version": "2.167"}],
            }

            headers = {"Content-Type": "application/json", "Referer": self.server_url}

            response = self.session.post(self.base_endpoint, headers=headers, json=request_data)

            if response.status_code == 200:
                result = response.json()
                if result.get("error"):
                    logger.error("Certificate revocation failed: %s", result["error"])
                    return False

                logger.info("Revoked certificate with serial %s", serial_number)
                return True
            logger.error(
                "Certificate revocation failed: %s - %s", response.status_code, response.text
            )
            return False

        except Exception as e:
            logger.error("Error revoking certificate: %s", e)
            raise

    def provision_certificate(
        self,
        hostname: str,
        service_type: str,
        output_dir: str = "/tmp/certs",
        sans: Optional[List[str]] = None,
    ) -> bool:
        """End-to-end: CSR, issue, fetch PEM, write mTLS sidecar config."""
        try:
            Path(output_dir).mkdir(parents=True, exist_ok=True)

            csr, private_key = self.generate_certificate_request(hostname, service_type, sans)

            cert_response = self.request_certificate(hostname, service_type, csr)
            if not cert_response:
                return False

            serial_number = cert_response.get("result", {}).get("serial_number")
            if serial_number is None:
                serial_number = cert_response.get("serial_number")
            if not serial_number:
                logger.error("No serial number in certificate response")
                return False

            time.sleep(2)

            cert_details = self.get_certificate(serial_number)
            if not cert_details:
                return False

            certificate = cert_details.get("certificate")
            if not certificate:
                logger.error("No certificate in response")
                return False

            cert_file = Path(output_dir) / f"{hostname}.crt"
            key_file = Path(output_dir) / f"{hostname}.key"
            chain_file = Path(output_dir) / f"{hostname}-chain.crt"

            with open(cert_file, "w") as f:
                f.write(certificate)

            with open(key_file, "w") as f:
                f.write(private_key)

            self.create_mtls_config(hostname, service_type, cert_file, key_file, chain_file)

            logger.info("Successfully provisioned certificate for %s", hostname)
            return True

        except Exception as e:
            logger.error("Error provisioning certificate: %s", e)
            return False

    def create_mtls_config(
        self, hostname: str, service_type: str, cert_file: Path, key_file: Path, chain_file: Path
    ):
        """Write JSON mTLS profile next to issued certs."""
        try:
            config = {
                "service": {
                    "hostname": hostname,
                    "type": service_type,
                    "tls": {
                        "certificate": str(cert_file),
                        "private_key": str(key_file),
                        "ca_certificate": str(chain_file),
                        "verify_client": "require",
                        "verify_depth": 2,
                        "ciphers": "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS",
                        "protocols": ["TLSv1.3", "TLSv1.2"],
                        "min_version": "TLSv1.2",
                    },
                },
                "security": {
                    "zero_trust": True,
                    "mutual_tls": True,
                    "certificate_validation": True,
                    "revocation_check": True,
                },
                "metadata": {
                    "provisioned_by": "Internal-CA-PKI-Provisioner",
                    "provisioned_at": datetime.utcnow().isoformat() + "Z",
                    "validity_days": CERT_VALIDITY_DAYS,
                    "key_size": CERT_KEY_SIZE,
                },
            }

            config_file = Path(cert_file.parent) / f"{hostname}-mtls.json"
            with open(config_file, "w") as f:
                json.dump(config, f, indent=2)

            logger.info("Created mTLS configuration: %s", config_file)

        except Exception as e:
            logger.error("Error creating mTLS config: %s", e)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Internal X.509 CA — PKI certificate provisioning (IPA-compatible API)"
    )
    parser.add_argument(
        "--action", choices=["request", "renew", "revoke"], required=True, help="Action to perform"
    )
    parser.add_argument(
        "--service",
        choices=["edge-gateway", "quantum-router", "wasm-enclave"],
        required=True,
        help="Service type",
    )
    parser.add_argument("--hostname", required=True, help="Hostname for certificate")
    parser.add_argument(
        "--output-dir", default="/tmp/certs", help="Output directory for certificates"
    )
    parser.add_argument("--sans", help="Comma-separated list of SANs")
    parser.add_argument("--reason", default="unspecified", help="Revocation reason")
    parser.add_argument("--username", help="CA admin username for authentication")
    parser.add_argument("--password", help="CA admin password for authentication")

    args = parser.parse_args()

    provisioner = InternalCAPKIProvisioner()

    if args.username and args.password:
        if not provisioner.authenticate(args.username, args.password):
            sys.exit(1)
    else:
        if not provisioner.authenticate():
            sys.exit(1)

    if args.action == "request":
        sans = args.sans.split(",") if args.sans else None
        success = provisioner.provision_certificate(
            args.hostname, args.service, args.output_dir, sans
        )
        sys.exit(0 if success else 1)

    if args.action == "renew":
        sans = args.sans.split(",") if args.sans else None
        success = provisioner.provision_certificate(
            args.hostname, args.service, args.output_dir, sans
        )
        sys.exit(0 if success else 1)

    if args.action == "revoke":
        logger.warning("Certificate revocation requires serial number tracking")
        logger.info("Use your internal CA web UI or operator CLI to revoke by serial")
        sys.exit(0)


# Backward compatibility for imports and external automation
FreeIPAPKIProvisioner = InternalCAPKIProvisioner

if __name__ == "__main__":
    main()
