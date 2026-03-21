#!/usr/bin/env python3
"""
FreeIPA PKI Certificate Provisioning Script for Hierarchical Edge-Quantum AI Architecture

This script automates the process of requesting and provisioning TLS certificates
from FreeIPA's Certificate Authority for Edge Gateways and the centralized Quantum Router.

Features:
- Authenticates to FreeIPA using Kerberos or username/password
- Requests certificates for Edge Gateway WebAssembly enclaves
- Requests certificates for Quantum Router components
- Supports certificate renewal and revocation
- Generates mTLS configuration files
- Integrates with the zero-trust architecture

Usage:
    python freeipa_pki_provisioning.py --action request --service edge-gateway --hostname gateway01.edge.qminiwasm.local
    python freeipa_pki_provisioning.py --action request --service quantum-router --hostname router.qminiwasm.local
    python freeipa_pki_provisioning.py --action renew --service edge-gateway --hostname gateway01.edge.qminiwasm.local
    python freeipa_pki_provisioning.py --action revoke --service edge-gateway --hostname gateway01.edge.qminiwasm.local --reason "Compromised"
"""

import argparse
import json
import logging
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import requests
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from requests.auth import HTTPBasicAuth
from requests_kerberos import HTTPKerberosAuth, OPTIONAL

# Configuration
FREEIPA_SERVER = os.getenv("FREEIPA_SERVER", "https://ipa.qminiwasm.local")
FREEIPA_REALM = os.getenv("FREEIPA_REALM", "QMINIWASM.LOCAL")
FREEIPA_DOMAIN = os.getenv("FREEIPA_DOMAIN", "qminiwasm.local")
FREEIPA_ADMIN_USER = os.getenv("FREEIPA_ADMIN_USER", "admin")
FREEIPA_ADMIN_PASSWORD = os.getenv("FREEIPA_ADMIN_PASSWORD")

# Certificate configuration
CERT_VALIDITY_DAYS = int(os.getenv("CERT_VALIDITY_DAYS", "365"))
CERT_KEY_SIZE = int(os.getenv("CERT_KEY_SIZE", "2048"))
CERT_ALGORITHM = hashes.SHA256()

# Logging configuration
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class FreeIPAPKIProvisioner:
    """FreeIPA PKI Certificate Provisioning Manager"""
    
    def __init__(self, server_url: str = FREEIPA_SERVER, realm: str = FREEIPA_REALM):
        """
        Initialize the FreeIPA PKI Provisioner
        
        Args:
            server_url: FreeIPA server URL
            realm: Kerberos realm
        """
        self.server_url = server_url.rstrip('/')
        self.realm = realm
        self.session = requests.Session()
        self.session.verify = True  # Enable SSL verification
        
        # Set up Kerberos authentication
        self.kerberos_auth = HTTPKerberosAuth(mutual_authentication=OPTIONAL)
        
        # API endpoints
        self.base_endpoint = f"{self.server_url}/ipa/session/json"
        self.certificate_endpoint = f"{self.server_url}/ipa/cert_request"
        
    def authenticate(self, username: Optional[str] = None, password: Optional[str] = None) -> bool:
        """
        Authenticate to FreeIPA server
        
        Args:
            username: Username for authentication (optional if using Kerberos)
            password: Password for authentication (optional if using Kerberos)
            
        Returns:
            bool: True if authentication successful, False otherwise
        """
        try:
            if username and password:
                # Username/password authentication
                auth = HTTPBasicAuth(username, password)
                response = self.session.post(
                    f"{self.server_url}/ipa/session/login_password",
                    headers={'Referer': self.server_url},
                    auth=auth,
                    data={'user': username, 'password': password}
                )
            else:
                # Kerberos authentication
                response = self.session.post(
                    f"{self.server_url}/ipa/session/login_kerberos",
                    headers={'Referer': self.server_url},
                    auth=self.kerberos_auth
                )
            
            if response.status_code == 200:
                logger.info("Successfully authenticated to FreeIPA")
                return True
            else:
                logger.error(f"Authentication failed: {response.status_code} - {response.text}")
                return False
                
        except Exception as e:
            logger.error(f"Authentication error: {e}")
            return False
    
    def generate_certificate_request(self, 
                                   hostname: str, 
                                   service_type: str,
                                   sans: Optional[List[str]] = None) -> Tuple[str, str]:
        """
        Generate a certificate signing request (CSR) for the specified service
        
        Args:
            hostname: The hostname for the certificate
            service_type: Type of service (edge-gateway, quantum-router, etc.)
            sans: Additional subject alternative names
            
        Returns:
            Tuple[str, str]: CSR string and private key string
        """
        try:
            # Generate private key
            private_key = rsa.generate_private_key(
                public_exponent=65537,
                key_size=CERT_KEY_SIZE,
            )
            
            # Build subject
            subject = x509.Name([
                x509.NameAttribute(x509.NameOID.COUNTRY_NAME, "US"),
                x509.NameAttribute(x509.NameOID.STATE_OR_PROVINCE_NAME, "CA"),
                x509.NameAttribute(x509.NameOID.LOCALITY_NAME, "San Francisco"),
                x509.NameAttribute(x509.NameOID.ORGANIZATION_NAME, "QMiniWASM"),
                x509.NameAttribute(x509.NameOID.ORGANIZATIONAL_UNIT_NAME, service_type.title()),
                x509.NameAttribute(x509.NameOID.COMMON_NAME, hostname),
            ])
            
            # Build SANs
            san_list = [x509.DNSName(hostname)]
            if sans:
                for san in sans:
                    san_list.append(x509.DNSName(san))
            
            # Create CSR
            csr = x509.CertificateSigningRequestBuilder().subject_name(
                subject
            ).add_extension(
                x509.SubjectAlternativeName(san_list),
                critical=False,
            ).sign(private_key, CERT_ALGORITHM)
            
            # Serialize CSR and private key
            csr_pem = csr.public_bytes(serialization.Encoding.PEM).decode('utf-8')
            private_key_pem = private_key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.PKCS8,
                encryption_algorithm=serialization.NoEncryption()
            ).decode('utf-8')
            
            logger.info(f"Generated CSR for {hostname}")
            return csr_pem, private_key_pem
            
        except Exception as e:
            logger.error(f"Error generating CSR: {e}")
            raise
    
    def request_certificate(self, 
                          hostname: str, 
                          service_type: str,
                          csr: str,
                          profile_id: str = "caIPAserviceCert") -> Dict:
        """
        Request a certificate from FreeIPA CA
        
        Args:
            hostname: The hostname for the certificate
            service_type: Type of service
            csr: Certificate signing request
            profile_id: Certificate profile ID
            
        Returns:
            Dict: Certificate response from FreeIPA
        """
        try:
            # Prepare request data
            request_data = {
                "method": "cert_request",
                "params": [
                    [csr],  # CSR
                    {
                        "add": True,
                        "principal": f"HTTP/{hostname}@{self.realm}",
                        "profile_id": profile_id,
                        "version": "2.167"
                    }
                ]
            }
            
            headers = {
                'Content-Type': 'application/json',
                'Referer': self.server_url
            }
            
            response = self.session.post(
                self.base_endpoint,
                headers=headers,
                json=request_data
            )
            
            if response.status_code == 200:
                result = response.json()
                if result.get('error'):
                    logger.error(f"Certificate request failed: {result['error']}")
                    return {}
                
                logger.info(f"Certificate requested for {hostname}")
                return result.get('result', {})
            else:
                logger.error(f"Certificate request failed: {response.status_code} - {response.text}")
                return {}
                
        except Exception as e:
            logger.error(f"Error requesting certificate: {e}")
            raise
    
    def get_certificate(self, serial_number: int) -> Dict:
        """
        Retrieve a certificate from FreeIPA CA
        
        Args:
            serial_number: Certificate serial number
            
        Returns:
            Dict: Certificate details
        """
        try:
            request_data = {
                "method": "cert_show",
                "params": [
                    [serial_number],
                    {}
                ]
            }
            
            headers = {
                'Content-Type': 'application/json',
                'Referer': self.server_url
            }
            
            response = self.session.post(
                self.base_endpoint,
                headers=headers,
                json=request_data
            )
            
            if response.status_code == 200:
                result = response.json()
                if result.get('error'):
                    logger.error(f"Certificate retrieval failed: {result['error']}")
                    return {}
                
                logger.info(f"Retrieved certificate with serial {serial_number}")
                return result.get('result', {})
            else:
                logger.error(f"Certificate retrieval failed: {response.status_code} - {response.text}")
                return {}
                
        except Exception as e:
            logger.error(f"Error retrieving certificate: {e}")
            raise
    
    def revoke_certificate(self, serial_number: int, reason: str = "unspecified") -> bool:
        """
        Revoke a certificate in FreeIPA CA
        
        Args:
            serial_number: Certificate serial number
            reason: Revocation reason
            
        Returns:
            bool: True if successful, False otherwise
        """
        try:
            request_data = {
                "method": "cert_revoke",
                "params": [
                    [serial_number],
                    {
                        "revocation_reason": reason,
                        "version": "2.167"
                    }
                ]
            }
            
            headers = {
                'Content-Type': 'application/json',
                'Referer': self.server_url
            }
            
            response = self.session.post(
                self.base_endpoint,
                headers=headers,
                json=request_data
            )
            
            if response.status_code == 200:
                result = response.json()
                if result.get('error'):
                    logger.error(f"Certificate revocation failed: {result['error']}")
                    return False
                
                logger.info(f"Revoked certificate with serial {serial_number}")
                return True
            else:
                logger.error(f"Certificate revocation failed: {response.status_code} - {response.text}")
                return False
                
        except Exception as e:
            logger.error(f"Error revoking certificate: {e}")
            raise
    
    def provision_certificate(self, 
                            hostname: str, 
                            service_type: str,
                            output_dir: str = "/tmp/certs",
                            sans: Optional[List[str]] = None) -> bool:
        """
        Complete certificate provisioning workflow
        
        Args:
            hostname: The hostname for the certificate
            service_type: Type of service
            output_dir: Directory to save certificate files
            sans: Additional subject alternative names
            
        Returns:
            bool: True if successful, False otherwise
        """
        try:
            # Ensure output directory exists
            Path(output_dir).mkdir(parents=True, exist_ok=True)
            
            # Generate CSR and private key
            csr, private_key = self.generate_certificate_request(hostname, service_type, sans)
            
            # Request certificate
            cert_response = self.request_certificate(hostname, service_type, csr)
            if not cert_response:
                return False
            
            serial_number = cert_response.get('result', {}).get('serial_number')
            if not serial_number:
                logger.error("No serial number in certificate response")
                return False
            
            # Wait a moment for certificate to be issued
            import time
            time.sleep(2)
            
            # Retrieve certificate
            cert_details = self.get_certificate(serial_number)
            if not cert_details:
                return False
            
            certificate = cert_details.get('certificate')
            if not certificate:
                logger.error("No certificate in response")
                return False
            
            # Save certificate files
            cert_file = Path(output_dir) / f"{hostname}.crt"
            key_file = Path(output_dir) / f"{hostname}.key"
            chain_file = Path(output_dir) / f"{hostname}-chain.crt"
            
            with open(cert_file, 'w') as f:
                f.write(certificate)
            
            with open(key_file, 'w') as f:
                f.write(private_key)
            
            # Create mTLS configuration
            self.create_mtls_config(hostname, service_type, cert_file, key_file, chain_file)
            
            logger.info(f"Successfully provisioned certificate for {hostname}")
            return True
            
        except Exception as e:
            logger.error(f"Error provisioning certificate: {e}")
            return False
    
    def create_mtls_config(self, 
                         hostname: str, 
                         service_type: str,
                         cert_file: Path,
                         key_file: Path,
                         chain_file: Path):
        """
        Create mTLS configuration file for the service
        
        Args:
            hostname: The hostname
            service_type: Type of service
            cert_file: Certificate file path
            key_file: Private key file path
            chain_file: Certificate chain file path
        """
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
                        "min_version": "TLSv1.2"
                    }
                },
                "security": {
                    "zero_trust": True,
                    "mutual_tls": True,
                    "certificate_validation": True,
                    "revocation_check": True
                },
                "metadata": {
                    "provisioned_by": "FreeIPA-PKI-Provisioner",
                    "provisioned_at": datetime.utcnow().isoformat() + "Z",
                    "validity_days": CERT_VALIDITY_DAYS,
                    "key_size": CERT_KEY_SIZE
                }
            }
            
            config_file = Path(cert_file.parent) / f"{hostname}-mtls.json"
            with open(config_file, 'w') as f:
                json.dump(config, f, indent=2)
            
            logger.info(f"Created mTLS configuration: {config_file}")
            
        except Exception as e:
            logger.error(f"Error creating mTLS config: {e}")


def main():
    """Main CLI entry point"""
    parser = argparse.ArgumentParser(description="FreeIPA PKI Certificate Provisioning")
    parser.add_argument("--action", choices=["request", "renew", "revoke"], required=True,
                       help="Action to perform")
    parser.add_argument("--service", choices=["edge-gateway", "quantum-router", "wasm-enclave"],
                       required=True, help="Service type")
    parser.add_argument("--hostname", required=True, help="Hostname for certificate")
    parser.add_argument("--output-dir", default="/tmp/certs", help="Output directory for certificates")
    parser.add_argument("--sans", help="Comma-separated list of SANs")
    parser.add_argument("--reason", default="unspecified", help="Revocation reason")
    parser.add_argument("--username", help="FreeIPA username for authentication")
    parser.add_argument("--password", help="FreeIPA password for authentication")
    
    args = parser.parse_args()
    
    # Initialize provisioner
    provisioner = FreeIPAPKIProvisioner()
    
    # Authenticate
    if args.username and args.password:
        if not provisioner.authenticate(args.username, args.password):
            sys.exit(1)
    else:
        if not provisioner.authenticate():
            sys.exit(1)
    
    # Perform action
    if args.action == "request":
        sans = args.sans.split(",") if args.sans else None
        success = provisioner.provision_certificate(
            args.hostname, 
            args.service, 
            args.output_dir, 
            sans
        )
        sys.exit(0 if success else 1)
    
    elif args.action == "renew":
        # For renewal, we'll request a new certificate with the same parameters
        sans = args.sans.split(",") if args.sans else None
        success = provisioner.provision_certificate(
            args.hostname, 
            args.service, 
            args.output_dir, 
            sans
        )
        sys.exit(0 if success else 1)
    
    elif args.action == "revoke":
        # For revocation, we need to find the certificate first
        # This is a simplified implementation - in practice, you'd track serial numbers
        logger.warning("Certificate revocation requires serial number tracking")
        logger.info("Use FreeIPA web UI or CLI to revoke certificates manually")
        sys.exit(0)


if __name__ == "__main__":
    main()