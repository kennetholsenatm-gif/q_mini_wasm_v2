---
description: How to build and deploy Q-Mini Incus native containers
---

# Q-Mini Incus Deployment Workflow

This workflow covers building and deploying the Incus native ternary runtime.

## Prerequisites

- Incus or LXD installed on target host
- CMake 3.16+
- C++20 compiler
- Linux environment (native or WSL2)

## Build Steps

### 1. Build the Binaries

```bash
cd q_mini_wasm_v2
mkdir -p build_incus && cd build_incus

# Configure (ternary-native, no WASM)
cmake ../q_mini_incus \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_INCUS_INFERENCE=ON \
    -DBUILD_INCUS_TRAINING=ON \
    -DBUILD_INCUS_WUI=ON \
    -DBUILD_INCUS_AGENTS=ON \
    -DBUILD_SINGLE_CONTAINER=ON

# Build
make -j$(nproc)
```

// turbo
### 2. Create Container Images

#### Single Container (Unified)

```bash
# Create base container
incus launch images:ubuntu/24.04 qmini-unified-build

# Copy binaries
incus file push build_incus/qmini-unified qmini-unified-build/root/
incus file push -r ../q_mini_incus/deploy/single-container/ qmini-unified-build/root/config/

# Install dependencies and configure
incus exec qmini-unified-build -- bash -c "
    apt-get update && apt-get install -y libstdc++6
    mkdir -p /var/lib/qmini/{models,datasets,ipc}
    chmod +x /root/qmini-unified
"

# Create image from container
incus stop qmini-unified-build
incus publish qmini-unified-build --alias qmini-unified
incus delete qmini-unified-build
```

#### Multi-Container (Individual Services)

```bash
# Build each service container
for service in inference training wui agents; do
    incus launch images:ubuntu/24.04 qmini-${service}-build
    incus file push build_incus/qmini-${service} qmini-${service}-build/root/
    # ... configure each service
    incus publish qmini-${service}-build --alias qmini-${service}
    incus delete qmini-${service}-build
done
```

### 3. Deploy

#### Single Container Mode

```bash
# Launch unified container
incus launch qmini-unified qmini-prod --profile q_mini_incus/deploy/single-container/incus-profile.yaml

# Check status
incus exec qmini-prod -- ps aux | grep qmini
incus exec qmini-prod -- cat /var/log/qminid.log
```

#### Multi-Container Mode

```bash
# Launch individual containers
incus launch qmini-inference qmini-infer-1 --profile qmini-multi
incus launch qmini-training qmini-train-1 --profile qmini-multi
incus launch qmini-wui qmini-wui-1 --profile qmini-multi

# Verify IPC connectivity
incus exec qmini-infer-1 -- ls -la /var/run/qmini/
```

## Verification

### Check Ternary-Native Mode

```bash
incus exec qmini-prod -- env | grep Q_MINI
# Should show:
# Q_MINI_INCUS_NATIVE=1
# Q_MINI_NO_WASM_BRIDGE=1
# Q_MINI_NAMESPACE_BOUNDARY=1
```

### Test Inference

```bash
# Single container - direct API test
curl http://$(incus list qmini-prod -f json | jq -r '.[0].state.network.eth0.addresses[0].address'):8080/api/inference \
    -X POST \
    -H "Content-Type: application/json" \
    -d '{"input": "test prompt"}'
```

### Monitor Services

```bash
# View qminid stats
incus exec qmini-prod -- /root/qmini-unified --stats

# Check logs
incus exec qmini-prod -- tail -f /var/log/qminid.log
```

## Troubleshooting

### Service Won't Start

1. Check environment variables are set
2. Verify IPC socket directory exists: `/var/run/qmini/`
3. Check namespace isolation: `incus exec qmini-prod -- lsns`

### Binary Contamination Warnings

If you see warnings about binary conversion:
- Verify `Q_MINI_NAMESPACE_BOUNDARY=1` is set
- Check boundary bridge is running in qminid

### IPC Connection Failures

1. Ensure all containers share IPC mount: `/var/run/qmini/`
2. Check permissions on socket files
3. Verify containers use same Incus profile

## Production Checklist

- [ ] Built with Release configuration
- [ ] Container limits set appropriately
- [ ] Shared storage mounted at `/var/lib/qmini/`
- [ ] IPC sockets accessible between containers
- [ ] API ports exposed through proxy devices
- [ ] Environment variables configured
- [ ] Logging configured
- [ ] Health checks enabled
