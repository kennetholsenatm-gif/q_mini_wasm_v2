# WUI Architecture - Single Entry Point Design

## The Golden Rule

**`C:\GitHub\q_mini_wasm_v2\qminiwasm.exe` is the ONLY executable the user ever needs to run.**

Everything else is a subprocess, library, or asset that qminiwasm.exe manages internally.

---

## What is the WUI?

The **WUI (Web User Interface)** is a desktop application with a web-based frontend:

- **Container**: Native desktop executable (`qminiwasm.exe`)
- **UI Layer**: HTML/CSS/JavaScript files in `wui/` directory
- **Bridge Layer**: Go-based MCP (Model Context Protocol) server (`wui-cli-bridge`)
- **Backend**: C++ inference and training engines

The WUI provides a browser-based interface that runs inside a native desktop shell, enabling:
- Quantum circuit visualization
- Training control and monitoring
- Model management
- Configuration editing
- Real-time SSE streaming of training progress

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           USER LAYER                                         │
│                                                                              │
│    ┌─────────────────────────────────────────────────────────────────┐      │
│    │  User runs: qminiwasm.exe                                       │      │
│    │  (C:\GitHub\q_mini_wasm_v2\qminiwasm.exe)                       │      │
│    └─────────────────────────────────────────────────────────────────┘      │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        WUI DESKTOP CONTAINER                               │
│                      (qminiwasm.exe - Go binary)                           │
│                                                                              │
│  ┌─────────────────┐    ┌──────────────────┐    ┌──────────────────┐     │
│  │  HTTP Server    │    │  MCP Server        │    │  SSE Server      │     │
│  │  (serves wui/)  │    │  (stdin/HTTP)      │    │  (port 9090)     │     │
│  │  Port: dynamic  │    │                    │    │                  │     │
│  └─────────────────┘    └──────────────────┘    └──────────────────┘     │
│           │                      │                       │                  │
│           ▼                      ▼                       ▼                  │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │                    SUBPROCESS MANAGER                                │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │  │
│  │  │ Trainer EXE  │  │  Inference   │  │   Other      │               │  │
│  │  │ (spawned on  │  │   Engine       │  │  subprocesses │               │  │
│  │  │  demand)     │  │                │  │               │               │  │
│  │  └──────────────┘  └──────────────┘  └──────────────┘               │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           FRONTEND LAYER                                     │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │  Browser/WebView renders:                                             ││
│  │  - wui/index.html (Dashboard)                                          ││
│  │  - wui/training.html (Training Control)                              ││
│  │  - wui/inference.html (Inference)                                    ││
│  │  - wui/config.html (Configuration)                                   ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │  JavaScript Bridges:                                                   ││
│  │  - mcp-host-bridge.js  → HTTP/MCP protocol                            ││
│  │  - training-sse-client.js → SSE streaming                             ││
│  └─────────────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Key Components

### 1. qminiwasm.exe (The WUI Executable)

**Location**: `C:\GitHub\q_mini_wasm_v2\qminiwasm.exe`

**Purpose**: The ONLY executable the user needs to run. It:
- Starts the HTTP server to serve WUI HTML files
- Initializes the MCP server for command handling
- Spawns the SSE server for training progress streaming
- Extracts embedded assets on first run
- Opens the default web browser automatically

**Build**: Produced by building `agents/cmd/wui-cli-bridge` with asset embedding

### 2. WUI Frontend

**Location**: `wui/` directory

**Files**:
- `index.html` - Main dashboard
- `training.html` - Training control and monitoring
- `inference.html` - Inference interface
- `config.html` - Configuration editor
- `js/mcp-host-bridge.js` - Bridge to backend
- `js/training-sse-client.js` - SSE streaming client

**Protocol**: Communicates via HTTP JSON-RPC to `/mcp` endpoint

### 3. MCP (Model Context Protocol) Server

**Location**: `agents/cmd/wui-cli-bridge/main.go`

**Purpose**: Handles tool calls from the WUI frontend:
- `wui_connect` - Connection handshake
- `wui_start_training_sse` - Spawns trainer subprocess
- `wui_stop_training_sse` - Stops trainer subprocess
- `wui_get_training_status` - Returns training state + process info
- And many more...

**Transport**: HTTP POST to `/mcp` endpoint or stdin/stdout for CLI mode

### 4. SSE (Server-Sent Events) Server

**Location**: `agents/cmd/wui-cli-bridge/sse_server.go`

**Purpose**: Real-time streaming of training progress:
- Port: 9090 (configurable via `training_config.toml`)
- Endpoint: `/training-stream`
- Streams stdout/stderr from trainer subprocess

### 5. Trainer Subprocess

**Executable**: `q_mini_wasm_v2_trainer.exe`

**Location Searched** (in order):
1. Project root: `q_mini_wasm_v2_trainer.exe`
2. Build release: `q_mini_wasm_v2/build_final/Release/q_mini_wasm_v2_trainer.exe`
3. Build debug: `q_mini_wasm_v2/build_final/Debug/q_mini_wasm_v2_trainer.exe`
4. Alternative: `build_final/Release/q_mini_wasm_v2_trainer.exe`
5. Current directory: `q_mini_wasm_v2_trainer.exe`
6. Custom path from `training_config.toml` `trainer_path` setting

**Lifecycle**:
- Spawned by `wui_start_training_sse` MCP call
- Runs with `--sse-mode` flag for streaming output
- Graceful shutdown via Ctrl+C, force kill after 5s timeout
- Exit code and error stored for status queries

---

## Subprocess Spawning Flow

When user clicks "Start Training" in the WUI:

```
1. User clicks button → JavaScript calls:
   window.wsBridge.callTool('wui_start_training_sse', {dataset_path: "..."})

2. MCP Host Bridge sends HTTP POST to:
   POST http://localhost:<port>/mcp
   {"jsonrpc": "2.0", "method": "wui_start_training_sse", "params": {...}}

3. SSE Server's handleMCPMethod receives the request

4. SSE Server calls StartTraining(projectRoot, args):
   - Finds trainer executable via findTrainerExecutable()
   - Spawns subprocess with exec.Command
   - Captures stdout/stderr pipes
   - Stores PID, path, start time
   - Starts goroutines to stream output to SSE clients

5. Returns JSON response:
   {
     "status": "training_started",
     "stream_url": "http://localhost:9090/training-stream",
     "trainer_path": "C:\\...\\q_mini_wasm_v2_trainer.exe",
     "pid": 12345
   }

6. WUI JavaScript connects to SSE stream and displays progress
```

---

## What NOT to Run Directly

These are INTERNAL components - do NOT run them directly:

| File | Why NOT to Run |
|------|----------------|
| `wui-cli-bridge.exe` | This IS qminiwasm.exe without assets - run qminiwasm.exe instead |
| `q_mini_wasm_v2_trainer.exe` | Should only be spawned by WUI as subprocess |
| `q_mini_wasm_v2_infer.exe` | Should only be spawned by WUI as subprocess |
| Individual WUI HTML files | Must be served by qminiwasm.exe HTTP server |
| Any scripts in `scripts/` | These are for CI/build, not user-facing |

---

## Configuration

### Training Config

**Location**: `config/training_config.toml`

Key settings for subprocess:
```toml
[streaming]
port = 9090  # SSE server port

# Optional: custom trainer path
trainer_path = "C:\\custom\\path\\q_mini_wasm_v2_trainer.exe"
```

---

## Troubleshooting

### "trainer executable not found"

The WUI searched these paths and failed:
1. `q_mini_wasm_v2_trainer.exe` (project root)
2. `q_mini_wasm_v2/build_final/Release/q_mini_wasm_v2_trainer.exe`
3. `q_mini_wasm_v2/build_final/Debug/q_mini_wasm_v2_trainer.exe`
4. `build_final/Release/q_mini_wasm_v2_trainer.exe`
5. Current working directory
6. Custom path from TOML (if set)

**Fix**: Build the trainer:
```bash
cmake --build q_mini_wasm_v2/build_final --target q_mini_wasm_v2_trainer
```

### "no training process running"

The trainer subprocess has exited or was never started. Check:
- SSE stream for error messages
- Process info via `wui_get_training_status`:
  ```json
  {
    "running": false,
    "exit_code": -1,
    "exit_error": "force killed"
  }
  ```

---

## Summary

| Do This | Not This |
|---------|----------|
| Run `qminiwasm.exe` | Run trainer/infer directly |
| Use WUI web interface | Try to start subprocesses manually |
| Configure via `training_config.toml` | Modify subprocess spawning code |
| Check SSE stream at `:9090/training-stream` | Parse log files manually |

---

**Remember**: `qminiwasm.exe` is the ONLY entry point. Everything else is an implementation detail.
