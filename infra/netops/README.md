# NetOps: NetBox, Netdisco, and SDN (VyOS)

This directory provides **network operations** tooling: a source of truth for inventory and IPAM, discovery and L2/L3 mapping, and reference SDN configuration for overlay networking.

## Components

| Component | Purpose |
|-----------|--------|
| **NetBox** | Source of truth for network inventory, IPAM, and device/VM metadata. Ansible can use NetBox as a [dynamic inventory](https://docs.ansible.com/ansible/latest/collections/community/general/netbox_inventory.html) for fleet automation. |
| **Netdisco** | Network discovery and mapping (IP/MAC, L2/L3 topology). Complements NetBox with live discovery data. |
| **VyOS (SDN)** | Reference VXLAN/BGP EVPN configuration for overlay fabric. Container networks (Docker bridges or Kubernetes CNI) can be extended to the fabric via Open vSwitch (OVS) or AsterNOS on the host. |

## Quick start (NetBox + Netdisco)

1. Copy environment and set secrets:
   ```bash
   cp .env.example .env
   # Edit .env: set POSTGRES_PASSWORD, REDIS_PASSWORD, SUPERUSER_PASSWORD, NETDISCO_*.
   ```
2. Start services:
   ```bash
   docker compose up -d
   ```
3. **NetBox:** <http://localhost:8000> — create sites, devices, and an API token for Ansible.  
4. **Netdisco:** <http://localhost:5000> — configure SNMP and run discovery.

## Ansible dynamic inventory from NetBox

The [infra/image-builder/ansible/](../image-builder/ansible/) directory includes `netbox_inventory.yml` and `ansible.cfg`. When running Ansible **against a NetBox-managed fleet** (not during the Packer build), set:

- `NETBOX_URL` — e.g. `http://<netbox-host>:8000`
- `NETBOX_TOKEN` — API token from NetBox (Admin → Users → Tokens)

Then run playbooks with inventory provided by NetBox (sites, roles, device types). For the **Packer build**, Packer injects a temporary inventory; the NetBox inventory is for post-deployment or fleet automation.

## SDN: VyOS VXLAN/BGP EVPN

The [sdn/vyos-config.set](sdn/vyos-config.set) file is a **template** of VyOS `set` commands that configure:

- Interfaces and a loopback for the VTEP
- VXLAN (VNI, local/remote VTEP)
- Bridge tying VXLAN to a LAN segment
- BGP EVPN (neighbor, advertise-all-vni, IPv4/IPv6)
- Firewall rules (allow VXLAN UDP 4789, BGP 179, ICMP; default drop)

Substitute the placeholders (`{{ VXLAN_VNI }}`, `{{ BGP_AS }}`, `{{ VTEP_LOCAL }}`, `{{ VTEP_REMOTE }}`, `{{ BGP_NEIGHBOR_IP }}`, `{{ LOOPBACK_IP }}`) before applying. You can apply the file manually on a VyOS instance or drive it via Ansible/expect.

### Connecting container networks to the fabric

Container networks (e.g. Docker bridge or Kubernetes CNI) can be attached to the VXLAN overlay so that workloads appear on the same L2 segment as the VyOS fabric:

- **Open vSwitch (OVS)** on the host: create an OVS bridge, add a VXLAN port with the VyOS VTEP as the remote endpoint, and attach the container network (e.g. bridge interface or veth) to that OVS bridge. OVS then forwards traffic over VXLAN to VyOS.
- **AsterNOS** (or similar): if the host runs AsterNOS, the same idea applies — the host acts as a VTEP and uses BGP EVPN to exchange MAC/IP with VyOS; container networks are attached to the AsterNOS bridge/VLAN that is carried in the EVPN overlay.

See your OVS or AsterNOS documentation for exact steps; this README only describes the VyOS side and the high-level integration point.

## Links

- [infra/image-builder/ansible/](../image-builder/ansible/) — Ansible playbooks and NetBox inventory
- [docker-compose.yml](docker-compose.yml) — NetBox + Netdisco stack
- [sdn/vyos-config.set](sdn/vyos-config.set) — VyOS VXLAN/BGP EVPN template
