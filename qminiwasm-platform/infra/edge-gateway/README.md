# Edge Gateway: Event-Driven AI with Solace Agent Mesh

This directory defines the **edge product**: edge LXC (or host) nodes that participate in the central event-driven AI mesh over **DMVPN** using **Solace Agent Mesh**. The edge agent mesh and its MCP tools are a primary product; the central data stack and security stack exist to run the broker and services that edge agents depend on. The architecture abandons synchronous REST APIs for AI workflows in favor of **asynchronous, Agent-to-Agent (A2A)** communication over the Solace event backbone, with **guaranteed delivery** so tasks survive temporary SASE/DMVPN tunnel drops.

## Event-Driven AI Paradigm

| Traditional (REST) | This architecture (Agent Mesh) |
|--------------------|---------------------------------|
| WUI/engine calls edge via HTTP | Central QminiWASM engine **publishes a task** to the event mesh |
| Request fails if network blips | **Orchestrator** routes task to the right edge agent; Solace **queues** until delivery |
| Tight coupling, point-to-point | **Agent Card**–based discovery; orchestrator picks agent by capability |
| No offline resilience | Edge agent **consumes from queue** when tunnel is back; no lost work |

### Flow

1. **Central (data stack):** The QminiWASM engine or a gateway publishes a **task request** (e.g. “analyze security logs at edge site X”) to a Solace topic/queue on the central PubSub+ broker.
2. **Orchestrator:** The Solace Agent Mesh **Orchestrator** (running in the central data stack) subscribes to task requests, consults **Agent Cards** (capabilities advertised by each agent), and **routes** the task to the appropriate edge agent (e.g. “Edge Security Log Analyzer” at the LXC with Wazuh).
3. **Edge agent:** The **edge sensor agent** runs in the LXC (or container), connects to the **central broker over the DMVPN tunnel** (using the tunnel IP as `SOLACE_BROKER_URL`). It **self-registers** its Agent Card (skills: “Edge Security Log Analyzer”, “Local Telemetry Summarizer”). When a task is routed to it, it runs its **tools** (e.g. MCP server that reads Wazuh alerts from the neighboring `security-ids` LXC) and sends the result back over the mesh.
4. **Guaranteed delivery:** If the SASE/DMVPN tunnel drops, the broker **retains** the task message. When the edge reconnects, the agent consumes the message and processes it. No manual retry or lost requests.

All **Solace broker credentials** (`SOLACE_BROKER_URL`, `SOLACE_BROKER_USERNAME`, `SOLACE_BROKER_PASSWORD`, `SOLACE_BROKER_VPN`) and optional **Wazuh/LLM** secrets must be supplied via **environment variables**. In production, source these from **HashiCorp Vault** (e.g. inject at container start or via Ansible/Vault integration); no hardcoded credentials in this repo.

## Directory layout

| Path | Purpose |
|------|--------|
| [ansible/site.yml](ansible/site.yml) | Playbook for edge hosts; applies `ai-agent-runtime` role (Agent Mesh CLI + config). |
| [ansible/roles/ai-agent-runtime/](ansible/roles/ai-agent-runtime/) | Installs Solace Agent Mesh CLI (pip/venv), deploys `agent-mesh/` YAML. |
| [agent-mesh/edge-sensor-agent.yaml](agent-mesh/edge-sensor-agent.yaml) | Edge agent definition: broker (DMVPN URL), **Agent Card** (skills), **Tool** (MCP for Wazuh). |
| [agent-mesh/tools/wazuh_alerts_mcp.py](agent-mesh/tools/wazuh_alerts_mcp.py) | MCP server that reads Wazuh alerts from the neighboring `security-ids` LXC/container. |
| [agent-mesh/shared_config_edge.yaml](agent-mesh/shared_config_edge.yaml) | Optional shared anchors for broker/model/services when running the edge agent standalone. |

## Central data stack (Solace + Agent Mesh)

The **central** Solace PubSub+ broker and Solace Agent Mesh (Orchestrator + Gateway) run in [containers/data-stack/](../containers/data-stack/). See that directory’s `docker-compose.yml` and `.env.example`:

- **solace-pubsub:** Event broker (SMF 55555, WebSocket 8008). Credentials via `SOLACE_ADMIN_PASSWORD` (and Vault when integrated).
- **solace-agent-mesh:** Orchestrator + Gateway; connects to `solace-pubsub` using `SOLACE_BROKER_URL`, `SOLACE_BROKER_PASSWORD`, etc. Exposes the web UI and routes tasks to edge agents by Agent Card.

Edge agents **do not** run in the data stack; they run on edge LXCs and connect to the **same** broker over DMVPN so the orchestrator and edge agents share one event mesh.

## Edge LXC setup

1. **Ansible (recommended):** Target your edge hosts with an inventory group `edge_gateways`. Run:
   ```bash
   ansible-playbook -i inventory.yml ansible/site.yml
   ```
   This installs the Solace Agent Mesh CLI (in a venv under `/opt/edge-gateway`) and deploys `agent-mesh/` (including `edge-sensor-agent.yaml` and `tools/wazuh_alerts_mcp.py`).

2. **Credentials:** Export broker and optional Wazuh/LLM vars (or inject from Vault):
   ```bash
   export SOLACE_BROKER_URL="ws://<central-broker-over-vpn>:8008"
   export SOLACE_BROKER_USERNAME="admin"
   export SOLACE_BROKER_PASSWORD="<from-vault-or-env>"
   export SOLACE_BROKER_VPN="default"
   export WAZUH_API_URL="http://security-ids:55000"
   export WAZUH_USER="wazuh-wui"
   export WAZUH_PASSWORD="<from-vault-or-env>"
   ```

3. **Run the edge agent:** From the project (or from `/opt/edge-gateway/agent-mesh` after Ansible):
   ```bash
   /opt/edge-gateway/venv/bin/sam run edge-sensor-agent.yaml
   ```
   Or from an Agent Mesh project that includes this agent config:
   ```bash
   sam run configs/agents/edge-sensor-agent.yaml
   ```

The agent connects to the central broker over DMVPN, publishes its **Agent Card** (Edge Security Log Analyzer, Local Telemetry Summarizer), and listens for tasks. The **Wazuh MCP tool** is invoked when the orchestrator sends a task that requires reading alerts from the neighboring `security-ids` LXC.

## Vault integration

- **Central stack:** Set `SOLACE_ADMIN_PASSWORD`, `SOLACE_BROKER_PASSWORD`, and optional `LLM_SERVICE_API_KEY` from Vault (e.g. `docker compose` with env file populated by `vault read -field=password secret/solace/admin` or an init container that writes env from Vault).
- **Edge LXC:** Before starting the edge agent, fetch Solace and Wazuh credentials from Vault and export them (e.g. `export SOLACE_BROKER_PASSWORD=$(vault read -field=password secret/edge/solace)`). Alternatively, use Ansible’s Vault integration to inject env into the systemd unit or container that runs `sam run`.

No credentials are stored in this repository; all use environment variables and Vault as the source of truth.

## References

- [Solace Agent Mesh](https://solacelabs.github.io/solace-agent-mesh/docs/documentation/getting-started/) — Orchestrator, Gateways, Agent Cards, A2A.
- [containers/data-stack/README.md](../containers/data-stack/README.md) — Central broker and Agent Mesh services.
