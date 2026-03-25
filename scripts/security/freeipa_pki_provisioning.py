#!/usr/bin/env python3
"""Deprecated entrypoint — use ``internal_ca_pki_provisioning`` (same CLI)."""

from internal_ca_pki_provisioning import FreeIPAPKIProvisioner, InternalCAPKIProvisioner, main

__all__ = ["FreeIPAPKIProvisioner", "InternalCAPKIProvisioner", "main"]

if __name__ == "__main__":
    main()
