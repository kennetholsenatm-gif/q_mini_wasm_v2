# WUI Trainer Subprocess Implementation Summary

## Changes Made

### 1. Enhanced SSE Server (`agents/cmd/wui-cli-bridge/sse_server.go`)

#### Added Process Metadata Tracking
- `trainerPath`: Stores the actual executable path used
- `trainerPID`: Process ID of running trainer
- `startTime`: When training started
- `exitCode`: Last exit code
- `exitError`: Last exit error message

#### Added Functions
- `findTrainerExecutable()`: Robust discovery with multiple search paths:
  1. Project root: `q_mini_wasm_v2_trainer.exe`
  2. Build release: `q_mini_wasm_v2/build_final/Release/q_mini_wasm_v2_trainer.exe`
  3. Build debug: `q_mini_wasm_v2/build_final/Debug/q_mini_wasm_v2_trainer.exe`
  4. Alternative: `build_final/Release/q_mini_wasm_v2_trainer.exe`
  5. Current working directory
  6. Custom path from `training_config.toml` `trainer_path` setting

- `isProcessRunning()`: Verifies process is actually alive
- `GetProcessInfo()`: Returns detailed process information
- `forceKill()`: Immediate process termination

#### Enhanced StopTraining()
- Graceful shutdown with `os.Interrupt` (Ctrl+C)
- 5-second timeout before force kill
- Proper exit code and error tracking

#### Enhanced StartTraining()
- Returns detailed error with all searched paths
- Stores process metadata
- Thread-safe state updates

#### Enhanced wui_start_training_sse Response
```json
{
  "status": "training_started",
  "stream_url": "http://localhost:9090/training-stream",
  "trainer_path": "C:\\...\\q_mini_wasm_v2_trainer.exe",
  "pid": 12345
}
```

### 2. Updated WUI Frontend (`wui/training.html`)

- Enhanced `doStartTraining()` to display trainer path and PID
- Uses `appendToStream()` to log process info to the UI

### 3. Documentation Created

#### `WUI_ARCHITECTURE.md`
- Complete architecture overview
- Component descriptions
- Subprocess spawning flow diagram
- "What NOT to run" section
- Configuration guide
- Troubleshooting section

#### `WUI_QUICK_REFERENCE.md`
- One-page quick reference
- User workflow diagram
- File locations table
- Common commands
- Architecture at a glance

### 4. Code Comments Added

Added architecture documentation references to:
- `sse_server.go` - Header comment with flow diagram
- `main.go` - Header comment explaining qminiwasm.exe is the only entry point
- `training.html` - HTML comment explaining subprocess spawning
- `mcp-host-bridge.js` - JSDoc comment explaining bridge architecture

## Key Behaviors

### Trainer Discovery
When user clicks "Start Training", the WUI searches for the trainer in order:
1. `q_mini_wasm_v2_trainer.exe` (project root - where qminiwasm.exe typically is)
2. `q_mini_wasm_v2/build_final/Release/q_mini_wasm_v2_trainer.exe`
3. `q_mini_wasm_v2/build_final/Debug/q_mini_wasm_v2_trainer.exe`
4. `build_final/Release/q_mini_wasm_v2_trainer.exe`
5. Current working directory
6. Custom path from `training_config.toml`

If not found, error shows ALL searched paths.

### Process Lifecycle
1. **Start**: Spawn with `--sse-mode`, capture stdout/stderr
2. **Stream**: Real-time SSE to browser
3. **Stop**: Send Ctrl+C, wait 5s, force kill if needed
4. **Monitor**: Track PID, exit code, error message

### Status API
`wui_get_training_status` now returns:
```json
{
  "running": true,
  "process_alive": true,
  "trainer_path": "C:\\...\\q_mini_wasm_v2_trainer.exe",
  "pid": 12345,
  "start_time": "2026-01-15T10:30:00Z",
  "elapsed_seconds": 120,
  "exit_code": 0,
  "exit_error": "",
  "epoch": 5,
  "samples_processed": 10000
}
```

## Testing Checklist

- [ ] Build trainer: `cmake --build q_mini_wasm_v2/build_final --target q_mini_wasm_v2_trainer`
- [ ] Build qminiwasm.exe: `go build -o qminiwasm.exe ./cmd/wui-cli-bridge` (from agents/)
- [ ] Run qminiwasm.exe and verify browser opens
- [ ] Click "Start Training" with valid dataset
- [ ] Verify trainer path and PID appear in stream
- [ ] Verify training progress streams correctly
- [ ] Click "Stop Training" - verify graceful shutdown
- [ ] Check status shows process info correctly
- [ ] Test error case: rename trainer, try to start, verify error shows all paths

## Architecture Reminder

```
┌─────────────────────────────────────────────────────┐
│  USER RUNS: qminiwasm.exe                           │
│           │                                          │
│           ▼                                          │
│  ┌──────────────────┐    ┌──────────────────────┐   │
│  │  WUI Frontend    │    │  MCP/SSE Server      │   │
│  │  (training.html) │◀──▶│  (in qminiwasm.exe) │   │
│  └──────────────────┘    └──────────────────────┘   │
│                                   │                  │
│           ┌───────────────────────┘                  │
│           ▼                                          │
│  ┌──────────────────────────────────────┐          │
│  │  q_mini_wasm_v2_trainer.exe          │          │
│  │  (spawned as subprocess)             │          │
│  └──────────────────────────────────────┘          │
└─────────────────────────────────────────────────────┘
```

**qminiwasm.exe is the ONLY entry point.**
