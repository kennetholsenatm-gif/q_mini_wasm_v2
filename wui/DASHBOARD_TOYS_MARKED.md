# Dashboard "Toys" Now Visually Marked

## What Changed

All placeholder/non-functional dashboard cards now have:
- `[NOT WIRED]` or `[READONLY]` in their titles
- Reduced opacity (0.7) to visually distinguish from working cards
- Warning text explaining what's missing

## Visual Indicators

| Card | Status | Visual Cues |
|------|--------|-------------|
| **Training Control** | ✅ Working | Full opacity, functional buttons |
| **Live Metrics** | ✅ Working | Full opacity, updates from SSE |
| **Betti Topology** | ✅ Working | Full opacity, updates from SSE |
| **Live Training Feed** | ✅ Working | Full opacity, SSE streaming |
| **Config Editor** | ✅ Working | Full opacity, editing functional |
| **Checkpoint Management** | ❌ Toy | 0.7 opacity, `[NOT WIRED]` label |
| **Model Configuration** | ❌ Toy | 0.7 opacity, `[READONLY]` label |
| **Inference Input** | ⚠️ Partial | Full opacity but output is toy |
| **Inference Output** | ❌ Toy | 0.7 opacity, `[NOT WIRED]` label |
| **Expert Activation** | ❌ Toy | 0.7 opacity, `[NOT WIRED]` label |
| **Topology Viz** | ❌ Toy | 0.7 opacity, `[NOT WIRED]` label |
| **Data Sources** | ❌ Toy | 0.7 opacity, `[NOT WIRED]` label |
| **Data Stats** | ❌ Toy | 0.7 opacity, `[NOT WIRED]` label |

## Why This Helps

1. **No More Confusion**: Users can immediately see what's functional vs placeholder
2. **Clear Expectations**: Warning text explains why something doesn't work
3. **Implementation Roadmap**: TODO comments in code show what endpoints are needed
4. **Priority Clarity**: Helps you know which cards to wire up first

## What Needs Backend Endpoints

To make the "toys" real:

1. **wui_list_checkpoints** - For checkpoint browser
2. **wui_restore_checkpoint** - For checkpoint restore
3. **wui_save_config** - For config editor to actually save
4. **wui_run_inference** - For inference panel
5. **wui_get_api_health** - For real API status
6. **wui_toggle_api** - For API on/off control

## Files Modified

- `wui/index.html` - Visual markers + TODO comments
- `wui/DASHBOARD_WIRING_STATUS.md` - Full documentation of wiring status

## Next Steps (If You Want to Wire Up)

1. Pick a card to implement
2. Add MCP handler in `agents/cmd/wui-cli-bridge/main.go`
3. Implement backend logic
4. Remove visual markers from the card
5. Update documentation
