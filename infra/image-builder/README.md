# Image Builder: AlmaLinux 9 Golden Image (Packer + QEMU)

Automated build pipeline for a **Golden Image** QCOW2 virtual appliance running **AlmaLinux 9**, pre-configured with a container runtime (**K3s**) and basic OS-level security hardening. Suitable for tactical edge deployments in defense/aerospace environments.

This aligns with the project's **DockerOS Platform Standard**: [docs/DockerOS-Platform-Standard.md](../../docs/DockerOS-Platform-Standard.md) (AlmaLinux 9 as host OS for Docker/Kubernetes nodes).

## Directory layout

```
infra/image-builder/
├── README.md           (this file)
└── packer/
    ├── almalinux-9.pkr.hcl
    ├── http/
    │   └── ks.cfg
    └── scripts/
        └── provision.sh
```

## Prerequisites

- **Packer** 1.7+  
  Install: [Packer downloads](https://developer.hashicorp.com/packer/downloads) or package manager (`choco install packer` on Windows, `dnf install packer` where available).
- **QEMU** (with `qemu-system-x86_64` and `qemu-img` in `PATH`)  
  Install: [QEMU](https://www.qemu.org/download/) (e.g. Windows: [qemu-weilnetz.de](https://qemu.weilnetz.de/); Linux: `dnf install qemu-kvm` or distro equivalent).
- **KVM** (Linux only): For faster builds, use KVM acceleration on a Linux host. On Windows, the template uses `kvm` by default; if QEMU is running on Windows, set `accelerator = "tcg"` in the source block or use WSL2 with KVM.

## Variables

| Variable         | Default (ISO / password) | Description |
|------------------|---------------------------|-------------|
| `iso_url`        | AlmaLinux 9 latest minimal ISO URL | Override for a mirror or pinned version. |
| `iso_checksum`   | SHA256 of default ISO     | Update when the ISO changes (build will fail if mismatch). |
| `ssh_password`   | `packer`                  | Root password set in kickstart and used by Packer to SSH. **Do not commit real passwords.** |
| `vm_name`        | `almalinux9-golden`       | Output QCOW2 filename. |
| `headless`       | `false`                   | Set `true` for CI (no QEMU GUI). |

Pass variables via environment or CLI:

- `PKR_VAR_ssh_password=yourpass packer build .`
- `packer build -var "ssh_password=secret" -var "headless=true" .`
- `packer build -var-file=my.pkrvars.hcl .`

Use a `-var-file` that is listed in `.gitignore` for secrets.

## Commands

Run from **`infra/image-builder/packer/`**:

```bash
cd infra/image-builder/packer/

# Install required Packer plugins (QEMU)
packer init .

# Validate and format HCL
packer validate .
packer fmt .

# Build the image (override password in production)
packer build .
# Or: packer build -var "ssh_password=YourSecurePassword" .
```

**Output:** The QCOW2 artifact is written to:

- `infra/image-builder/packer/output-almalinux9/almalinux9-golden.qcow2`

Use this file as a Golden Image in your virtualization or cloud pipeline.

## Boot command tuning

The template uses a `boot_command` that appends `inst.ks=http://{{ .HTTPIP }}:{{ .HTTPPort }}/ks.cfg` to the kernel command line. The AlmaLinux 9 minimal ISO boot menu can vary by version. If the build fails while **waiting for SSH**, the installer may not have started in text mode with the kickstart:

1. Run with a visible display (set `headless = false`) and watch the boot menu.
2. Adjust `boot_command` in `almalinux-9.pkr.hcl` (e.g. press `c` for command line, or select the correct menu entry with arrow keys, then **Tab** to edit and append `inst.ks=...`).

## Using Docker instead of K3s

The default provision script installs **K3s**. To use **Docker** instead:

1. In `packer/scripts/provision.sh`, replace the K3s install block with Docker (e.g. add Docker CE repo and `dnf install -y docker-ce`; `systemctl enable --now docker`).
2. Adjust firewall rules in the same script (e.g. allow Docker daemon ports if needed; for local use, socket-only is often enough).
3. Rebuild with `packer build .`.

## RHEL 9 / AlmaLinux 9 hosts

If you run Packer on RHEL 9 or AlmaLinux 9 and see CPU-related boot failures, set in the `source "qemu" "almalinux9"` block:

```hcl
cpu_model = "host"
```

## Security notes

- The default `ssh_password` and kickstart root password are set to `packer` for automation only. Change them for production (e.g. `-var` or `-var-file`) and never commit real passwords.
- The image disables root SSH login and creates an `admin` user with sudo; add SSH keys for `admin` after first boot or via your config management.
