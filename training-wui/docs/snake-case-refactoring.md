# Snake_case Refactoring Plan for Training WUI

## Overview

The existing `index.html` is ~3800 lines of JavaScript using camelCase naming (e.g., `telemetryStreamFilter`, `RunpodServerlessMetaDefaults`). The cognitive ergonomics research recommends **snake_case** for faster visual scanning (saccadic fluency).

Per the research:
> "Eye-tracking studies show that subjects recognize identifiers formatted in snake_case significantly more quickly than those formatted in camelCase. The explicit underscore in snake_case acts as a clear visual boundary between words."

## Scope

- **Files affected:** `training-wui/web/index.html` (~3800 lines)
- **Go backend:** No changes (Go uses camelCase for exported functions per convention)
- **New modules:** Already use snake_case (utils.js, schema-introspection.js, etc.)

## Refactoring Strategy

### Phase 1: Function names (highest impact)
```
camelCase → snake_case
telemetryStreamFilter → telemetry_stream_filter
telemetryCascadeStagesOnly → telemetry_cascade_stages_only
connectRunTelemetryWS → connect_run_telemetry_ws
pushTelemetryPoint → push_telemetry_point
appendTelemetryMetricRow → append_telemetry_metric_row
loadPreflight → load_preflight
loadConfigs → load_configs
buildPayload → build_payload
refreshModelFacts → refresh_model_facts
toggleHFFields → toggle_hf_fields
syncTrainingWuiQueryToURL → sync_training_wui_query_to_url
wireStopControls → wire_stop_controls
initDashboardLayouts → init_dashboard_layouts
initTelemetryCharts → init_telemetry_charts
```

### Phase 2: Variables (medium impact)
```
activeRunId → active_run_id
logOffset → log_offset
logPlainAccum → log_plain_accum
stickyLastEpoch → sticky_last_epoch
combatorialWallMarkerDone → combinatorial_wall_marker_done
telemetryMetricCount → telemetry_metric_count
telemetryStreamEventCount → telemetry_stream_event_count
```

### Phase 3: Constants (low impact)
```
TELEMETRY_SOURCE_CPP → TELEMETRY_SOURCE_CPP (already uppercase)
TELEMETRY_ROW_CAP → TELEMETRY_ROW_CAP (already uppercase)
```

## Execution

1. Use regex find/replace with word boundaries
2. Run existing tests to verify no regressions
3. Test WebSocket telemetry streaming
4. Test all tab switches and modal dialogs

## Note

This refactoring is **cosmetic** and does not change behavior. It improves code readability per the cognitive ergonomics research but is lower priority than functional improvements.

**Recommendation:** Do this pass **after** validating the new modular WUI works correctly with the existing Go backend.