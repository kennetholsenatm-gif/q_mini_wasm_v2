# WUI Dashboard Wiring Status

## Summary

Many dashboard cards are **visual placeholders** - they look functional but don't actually connect to the backend. This document tracks what's real vs what's cosmetic.

---

## ✅ FULLY WIRED (Working)

### Training Control Card
- **Start Training**: Calls `wui_start_training_sse` → spawns trainer subprocess ✓
- **Stop Training**: Calls `wui_stop_training_sse` → kills trainer ✓
- **Continuous Mode**: Checkbox syncs with config file ✓
- **Status Indicator**: Shows training state (idle/active/error) ✓

### Live Metrics Card
- **Epoch**: Updates from SSE `epoch` messages ✓
- **Progress**: Updates from SSE `progress` messages ✓
- **Active Experts**: Updates from SSE ✓
- **Topology Updates**: Updates from SSE ✓
- **Mode**: Reads from checkbox state ✓
- **Iteration**: Updates from SSE `continuous_cycle_complete` ✓

### Betti Topology Card
- **β₁ avg/max**: Updates from SSE `topology` messages ✓
- **Graph Density**: Updates from SSE ✓
- **Edges**: Updates from SSE ✓
- **Expert Splits**: Updates from SSE ✓

### Live Training Feed
- **SSE Streaming**: Connects to `localhost:9091/training-stream` ✓
- **Log Display**: Shows real trainer stdout/stderr ✓
- **Auto-scroll**: Follows new entries ✓

### Checkpoint Management - ✅ FULLY WIRED
- **Browse Checkpoints**: `wui_list_checkpoints` → Lists checkpoint files with metadata ✓
- **Checkpoint List**: Shows name, epoch, size, date ✓
- **Restore**: `wui_restore_checkpoint` → Updates config to resume from checkpoint ✓
- **Delete**: `wui_delete_checkpoint` → Removes checkpoint file ✓

### Config Editor (CLI Editor) - ✅ FULLY WIRED
- **TOML Editing**: Full text editor with syntax highlighting ✓
- **Tab Completion**: Suggests keys/values from schema ✓
- **Context Help**: Press `?` for field documentation ✓
- **Continuous Mode Sync**: Checkbox ↔ config two-way sync ✓
- **Save Config**: Calls `wui_save_config` → Writes to config/*.toml ✓
- **Copy Buttons**: Copies card content to clipboard ✓
- **Reset**: Reloads default config ✓

### Logging Panel
- **Log Display**: Shows timestamped messages ✓
- **Level Styling**: info/warn/error colors ✓
- **Auto-scroll**: Follows new entries ✓

---

## ⚠️ PARTIALLY WIRED (Stubs/Placeholders)

### Data Sources Panel - ✅ FULLY WIRED (Dynamic + Acquisition)
- **Dynamic Configuration**: `config/data_sources.toml` - persisted between sessions ✓
- **List Sources**: `wui_list_data_sources` → Returns all configured web APIs + local files ✓
- **Add Source**: `wui_add_data_source` → Prompt dialog for web API, local file, or directory ✓
- **Remove Source**: `wui_remove_data_source` → Right-click to delete from config ✓
- **Update Source**: `wui_update_data_source` → Toggle enabled/disabled ✓
- **API Health Check**: `wui_get_api_health` → Real HTTP checks with custom headers support ✓
- **Visual Status**: 🌐=web API, 📁=local file, Green=healthy, Red=error, Grey=disabled ✓
- **Data Acquisition**: Start/Pause/Stop with progress monitoring ✓
- **Live Progress**: Sources done, items processed, duration, errors ✓

### Inference Panel - 🔄 PARTIALLY WIRED
- **Input Text**: Accepts input ✓
- **Run Inference**: Calls `wui_run_inference` → Backend validates, finds checkpoint ⚠️
- **Output Display**: Shows error until trainer supports inference ❌
- **Expert Activation Viz**: UI ready, needs trainer data ❌

**Note**: Backend endpoint `wui_run_inference` exists and:
- Validates input text ✓
- Finds/validates checkpoint ✓
- Returns error: "trainer_main.cpp needs --inference mode support"

### Topology Panel - ✅ FULLY WIRED
- **Graph Visualization**: Canvas-based live graph rendering ✓
- **Topology Graph Stream**: `export_topology_json()` outputs nodes + edges ✓
- **Node Properties**: Generation, activation_count, beta_1, goodness ✓
- **Live Updates**: Updates every epoch from trainer SSE ✓
- **Color Coding**: Hue based on generation (seed=0 → children=1+) ✓
- **Size Coding**: Based on activation count ✓
- **Edge Rendering**: Shows expert connections ✓
- **Topology Evolution**: Live metrics from training data ✓

---

## What Needs Implementation

### High Priority - ✅ DONE

1. **Checkpoint Management** ✅
   - ~~Backend endpoint to list checkpoints~~ DONE
   - ~~Browse/restore functionality~~ DONE
   - ~~Delete old checkpoints~~ DONE

2. **Config Save** ✅
   - ~~Actually send TOML content to backend via MCP~~ DONE
   - ~~Backend writes to `config/training_config.toml`~~ DONE
   - ~~Validation before save~~ Basic validation DONE

3. **Data Sources** ✅
   - ~~Dynamic config in `config/data_sources.toml`~~ DONE
   - ~~Add/remove/update data sources via WUI~~ DONE
   - ~~Real API health checks~~ DONE
   - ~~`wui_start_data_acquisition` endpoint~~ DONE
   - ~~`wui_stop/pause/resume/get_status` endpoints~~ DONE
   - ~~Frontend acquisition controls (Start/Pause/Stop)~~ DONE
   - ~~Progress monitoring~~ DONE
   - ~~Python data acquisition script~~ DONE

4. **Inference Panel** 
   - ~~Backend inference endpoint~~ DONE (returns error until trainer supports it)
   - Model loading from checkpoint (needs C++ inference mode)
   - Expert activation visualization (UI ready, needs data)

### Medium Priority

4. **Inference Engine**
   - Model loading from checkpoint
   - Forward pass for inference
   - Expert activation tracking

### Low Priority

5. **Pause/Resume** ✅
   - ~~SIGSTOP/SIGCONT equivalent on Windows~~ DONE (NtSuspendProcess/NtResumeProcess)
   - ~~UI indicator for paused state~~ DONE

6. **Topology Visualization** ✅
   - ~~Canvas-based graph rendering~~ DONE
   - ~~Live topology updates from trainer~~ DONE
   - ~~Node coloring by generation~~ DONE

7. **History Navigation**
   - Command history in CLI editor
   - Up/Down arrow recall

---

## Backend Endpoints Needed

| Endpoint | Status | Purpose |
|----------|--------|---------|
| `wui_save_config` | ✅ **DONE** | Save TOML config file |
| `wui_list_checkpoints` | ✅ **DONE** | List available checkpoints |
| `wui_restore_checkpoint` | ✅ **DONE** | Update config to resume from checkpoint |
| `wui_delete_checkpoint` | ✅ **DONE** | Delete checkpoint file |
| `wui_run_inference` | 🔄 **WIRED - NEEDS TRAINER** | Endpoint exists, trainer needs inference mode |
| `wui_pause_training` | ✅ **DONE** | Suspend trainer process (NtSuspendProcess) |
| `wui_resume_training` | ✅ **DONE** | Resume trainer process (NtResumeProcess) |
| `wui_list_data_sources` | ✅ **DONE** | List configured web APIs + local files |
| `wui_add_data_source` | ✅ **DONE** | Add new data source to config |
| `wui_remove_data_source` | ✅ **DONE** | Remove data source from config |
| `wui_update_data_source` | ✅ **DONE** | Update/enable/disable data source |
| `wui_get_api_health` | ✅ **DONE** | Check API health with custom headers |
| `wui_start_data_acquisition` | ✅ **DONE** | Start crawling APIs/ingesting files |
| `wui_stop_data_acquisition` | ✅ **DONE** | Stop acquisition process |
| `wui_pause_data_acquisition` | ✅ **DONE** | Pause acquisition (NtSuspendProcess) |
| `wui_resume_data_acquisition` | ✅ **DONE** | Resume acquisition (NtResumeProcess) |
| `wui_get_data_acquisition_status` | ✅ **DONE** | Get progress/stats |

---

## Files to Modify

### Frontend (`wui/index.html`)
- Add real implementations for stub functions
- Wire up API toggle buttons
- Replace placeholder visualizations

### Backend (`agents/cmd/wui-cli-bridge/`)
- `main.go`: Add MCP handlers for missing endpoints
- `sse_server.go`: Already handles training streaming

### Trainer (`q_mini_wasm_v2/core/training/`)
- Add checkpoint listing support
- Add inference mode

---

## Testing Checklist

- [ ] Start/stop training works
- [ ] Metrics update in real-time
- [ ] Config changes persist after save
- [ ] Checkpoints can be browsed and restored
- [ ] Inference returns real results
- [ ] API toggles affect data acquisition
- [ ] Topology viz shows expert graph
