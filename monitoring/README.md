# Monitoring

See also [`docs/PIPELINE_STATEFUL_WASM_OPS.md`](../docs/PIPELINE_STATEFUL_WASM_OPS.md) for how SOA metrics fit the Stateful WASM ops blueprint.

## Grafana — SOA placeholders

The dashboard [`grafana/dashboards/qminiwasm-overview.json`](grafana/dashboards/qminiwasm-overview.json) includes a text panel listing **Stateful Operational Autonomy (SOA)** metric names intended for Prometheus (LCI, LME, SML, TtC, LMS/TBR). Wire real exporters to match those names when telemetry is implemented.

## Scripts

- `python scripts/benchmark_soa_migration.py` — State Migration Latency (WLES envelope build; optional zlib).
