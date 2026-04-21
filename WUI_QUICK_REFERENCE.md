# WUI Quick Reference

## The One Thing to Remember

```
┌─────────────────────────────────────────────────────────┐
│                                                         │
│   START HERE:  qminiwasm.exe                           │
│   Location: C:\GitHub\q_mini_wasm_v2\qminiwasm.exe      │
│                                                         │
│   This is the ONLY file you ever need to run.          │
│   Everything else happens automatically.               │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

---

## User Workflow

```
qminiwasm.exe
      │
      ▼
[Browser Opens]
      │
      ▼
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│  Dashboard  │───▶│  Training   │───▶│   Results   │
│  (index.html)    │  (training.html)   │   (graphs)  │
└─────────────┘    └─────────────┘    └─────────────┘
                            │
                            ▼
                  [q_mini_wasm_v2_trainer.exe]
                  (spawned automatically as subprocess)
```

---

## Architecture at a Glance

```
USER
 │
 │ runs
 ▼
qminiwasm.exe ──┬── HTTP Server (serves wui/*.html)
                ├── MCP Server (handles commands)
                ├── SSE Server (training progress stream)
                └── Subprocess Spawner
                         │
                         ▼
                q_mini_wasm_v2_trainer.exe
                         │
                         └── streams progress back to WUI
```

---

## File Locations

| Component | Location | User Touches? |
|-----------|----------|---------------|
| **WUI Executable** | `qminiwasm.exe` | **YES** - Run this |
| Frontend | `wui/*.html` | No - Served by qminiwasm |
| Bridge Code | `agents/cmd/wui-cli-bridge/*.go` | No - Compiled into qminiwasm |
| Trainer | `q_mini_wasm_v2_trainer.exe` | No - Spawned automatically |
| Config | `config/training_config.toml` | Optional - Edit for settings |

---

## Common Commands (For Developers)

### Build qminiwasm.exe
```bash
cd agents
go build -o ../qminiwasm.exe ./cmd/wui-cli-bridge
```

### Build Trainer (if missing)
```bash
cmake --build q_mini_wasm_v2/build_final --target q_mini_wasm_v2_trainer
```

### Run Everything (The Only Command You Need)
```bash
./qminiwasm.exe
```

---

## What Each File Actually Is

### qminiwasm.exe
- **What**: Desktop WUI container with embedded web assets
- **Contains**: Go binary + embedded wui/*.html + MCP server + SSE server
- **Spawns**: Trainer subprocess when user clicks "Start Training"
- **You**: Double-click this to open the WUI

### q_mini_wasm_v2_trainer.exe
- **What**: C++ training executable
- **Built from**: `q_mini_wasm_v2/core/training/trainer_main.cpp`
- **Run by**: qminiwasm.exe (never run directly)
- **You**: Never touch this directly

### wui/training.html
- **What**: Training control interface
- **Served by**: qminiwasm.exe HTTP server
- **Communicates via**: HTTP MCP protocol to qminiwasm.exe
- **You**: Edit this to change UI, but run qminiwasm.exe to see it

### agents/cmd/wui-cli-bridge/*.go
- **What**: Go source code for the bridge
- **Compiles to**: qminiwasm.exe
- **You**: Edit these files, rebuild qminiwasm.exe to test

---

## Key Insight

The WUI is a **desktop app that looks like a website**:
- It's NOT a website you host
- It's NOT a server you connect to remotely
- It IS a single .exe that opens a browser to localhost
- The browser IS the UI layer
- The .exe IS the backend + frontend server + subprocess manager

```
Traditional App:    App.exe → Window UI
WUI Architecture:   qminiwasm.exe → Browser → HTML/CSS/JS UI
```

---

## If Something Breaks

| Problem | Check |
|---------|-------|
| "trainer not found" | Build trainer first: `cmake --build ... --target q_mini_wasm_v2_trainer` |
| WUI won't open | Check if port is already in use |
| Training won't start | Check SSE stream at `localhost:9090/training-stream` |
| Changes not showing | Rebuild qminiwasm.exe to embed new assets |

---

## Bottom Line

```
┌──────────────────────────────────────────────────────┐
│                                                      │
│   Run: qminiwasm.exe                                │
│                                                      │
│   Everything else is automatic.                     │
│                                                      │
└──────────────────────────────────────────────────────┘
```
