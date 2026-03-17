import { useCallback, useEffect, useState } from "react";
import {
  hardwareApi,
  quantumApi,
  type ExecutionMode,
  type HardwareCurrent,
  type HardwareOption,
  type HpcConnectionDetails,
  type TransferConfig,
} from "../api/client";

export function HardwareConfig() {
  const [options, setOptions] = useState<HardwareOption[]>([]);
  const [current, setCurrent] = useState<HardwareCurrent | null>(null);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [executionMode, setExecutionMode] = useState<ExecutionMode>("hpc");
  const [localAccelerator, setLocalAccelerator] = useState<"cuda" | "xpu" | "cpu">("cpu");
  const [simulatedQuantum, setSimulatedQuantum] = useState(false);
  const [hpcConnection, setHpcConnection] = useState<HpcConnectionDetails>({});
  const [transferConfig, setTransferConfig] = useState<TransferConfig | null>(null);
  const [transferSaving, setTransferSaving] = useState(false);
  const [transferBatchSize, setTransferBatchSize] = useState(64);
  const [serializationFormat, setSerializationFormat] = useState("json");
  const [pollingInterval, setPollingInterval] = useState(2);

  const load = useCallback(() => {
    Promise.all([
      hardwareApi.getOptions(),
      hardwareApi.getCurrent(),
      quantumApi.getTransferConfig(),
    ])
      .then(([opts, cur, tc]) => {
        setOptions(opts);
        setCurrent(cur);
        setExecutionMode((cur.execution_mode as ExecutionMode) || "hpc");
        setLocalAccelerator((cur.local_accelerator as "cuda" | "xpu" | "cpu") || "cpu");
        setSimulatedQuantum(cur.simulated_quantum ?? false);
        setHpcConnection(cur.hpc_connection ?? {});
        setTransferConfig(tc);
        setTransferBatchSize(tc.transfer_batch_size);
        setSerializationFormat(tc.serialization_format);
        setPollingInterval(tc.polling_interval_seconds);
      })
      .finally(() => setLoading(false));
  }, []);

  useEffect(() => {
    load();
  }, [load]);

  const handleSelectExecutionMode = async (mode: ExecutionMode) => {
    setSaving(true);
    try {
      const updated = await hardwareApi.setCurrent({
        accelerator: current?.accelerator ?? "cpu",
        device_index: current?.device_index ?? 0,
        execution_mode: mode,
        ...(mode === "local"
          ? { local_accelerator: localAccelerator, simulated_quantum: simulatedQuantum }
          : { hpc_connection: hpcConnection }),
      });
      setCurrent(updated);
      setExecutionMode(mode);
    } finally {
      setSaving(false);
    }
  };

  const handleSelectLocalAccelerator = async (acc: "cuda" | "xpu" | "cpu") => {
    setLocalAccelerator(acc);
    setSaving(true);
    try {
      const updated = await hardwareApi.setCurrent({
        accelerator: acc,
        device_index: current?.device_index ?? 0,
        execution_mode: "local",
        local_accelerator: acc,
        simulated_quantum: simulatedQuantum,
      });
      setCurrent(updated);
    } finally {
      setSaving(false);
    }
  };

  const handleSimulatedQuantumChange = async (checked: boolean) => {
    setSimulatedQuantum(checked);
    setSaving(true);
    try {
      const updated = await hardwareApi.setCurrent({
        accelerator: current?.accelerator ?? "cpu",
        device_index: current?.device_index ?? 0,
        execution_mode: "local",
        local_accelerator: localAccelerator,
        simulated_quantum: checked,
      });
      setCurrent(updated);
    } finally {
      setSaving(false);
    }
  };

  const handleSelectHpcAccelerator = async (accelerator: "cuda" | "xpu" | "cpu") => {
    if (!current) return;
    setSaving(true);
    try {
      const updated = await hardwareApi.setCurrent({
        accelerator,
        device_index: current.device_index,
        execution_mode: "hpc",
        hpc_connection: hpcConnection,
      });
      setCurrent(updated);
    } finally {
      setSaving(false);
    }
  };

  const handleSaveHpcConnection = async () => {
    setSaving(true);
    try {
      const updated = await hardwareApi.setCurrent({
        accelerator: current?.accelerator ?? "cpu",
        device_index: current?.device_index ?? 0,
        execution_mode: "hpc",
        hpc_connection: hpcConnection,
      });
      setCurrent(updated);
    } finally {
      setSaving(false);
    }
  };

  const handleSaveTransferConfig = async () => {
    setTransferSaving(true);
    try {
      const tc = await quantumApi.setTransferConfig({
        transfer_batch_size: transferBatchSize,
        serialization_format: serializationFormat,
        polling_interval_seconds: pollingInterval,
      });
      setTransferConfig(tc);
    } finally {
      setTransferSaving(false);
    }
  };

  if (loading) return <section className="panel">Loading hardware options…</section>;

  return (
    <section className="panel">
      <h2>Hardware</h2>
      <p className="muted">Choose local hardware or HPC execution and configure connection.</p>

      <div className="form">
        <label>
          <span>Execution mode</span>
          <div className="option-list">
            <label className="option">
              <input
                type="radio"
                name="execution_mode"
                checked={executionMode === "local"}
                onChange={() => handleSelectExecutionMode("local")}
                disabled={saving}
              />
              <span>Local Hardware</span>
            </label>
            <label className="option">
              <input
                type="radio"
                name="execution_mode"
                checked={executionMode === "hpc"}
                onChange={() => handleSelectExecutionMode("hpc")}
                disabled={saving}
              />
              <span>HPC</span>
            </label>
          </div>
        </label>
      </div>

      {executionMode === "local" && (
        <div className="form">
          <p className="muted">Select local CPU, GPU, or simulated quantum.</p>
          <div className="option-list">
            {options.map((opt) => (
              <label key={opt.id} className="option">
                <input
                  type="radio"
                  name="local_accelerator"
                  checked={localAccelerator === opt.id}
                  onChange={() => handleSelectLocalAccelerator(opt.id as "cuda" | "xpu" | "cpu")}
                  disabled={saving}
                />
                <span>{opt.name}</span>
              </label>
            ))}
          </div>
          <label className="option">
            <input
              type="checkbox"
              checked={simulatedQuantum}
              onChange={(e) => handleSimulatedQuantumChange(e.target.checked)}
              disabled={saving}
            />
            <span>Simulated Quantum</span>
          </label>
        </div>
      )}

      {executionMode === "hpc" && (
        <>
          <div className="form">
            <p className="muted">Select accelerator for HPC node.</p>
            <div className="option-list">
              {options.map((opt) => (
                <label key={opt.id} className="option">
                  <input
                    type="radio"
                    name="accelerator"
                    checked={current?.accelerator === opt.id}
                    onChange={() => handleSelectHpcAccelerator(opt.id as "cuda" | "xpu" | "cpu")}
                    disabled={saving}
                  />
                  <span>{opt.name}</span>
                </label>
              ))}
            </div>
          </div>
          <div className="form">
            <h3>HPC connection</h3>
            <label>
              <span>Endpoint URL</span>
              <input
                type="url"
                value={hpcConnection.endpoint_url ?? ""}
                onChange={(e) => setHpcConnection((c) => ({ ...c, endpoint_url: e.target.value }))}
                placeholder="https://cluster.example.com"
              />
            </label>
            <label>
              <span>Authentication token</span>
              <input
                type="password"
                value={hpcConnection.auth_token ?? ""}
                onChange={(e) => setHpcConnection((c) => ({ ...c, auth_token: e.target.value }))}
                placeholder="Token (not echoed)"
              />
            </label>
            <label>
              <span>Cluster ID</span>
              <input
                type="text"
                value={hpcConnection.cluster_id ?? ""}
                onChange={(e) => setHpcConnection((c) => ({ ...c, cluster_id: e.target.value }))}
                placeholder="Cluster identifier"
              />
            </label>
            <label>
              <span>Node ID</span>
              <input
                type="text"
                value={hpcConnection.node_id ?? ""}
                onChange={(e) => setHpcConnection((c) => ({ ...c, node_id: e.target.value }))}
                placeholder="Node identifier"
              />
            </label>
            <button type="button" onClick={handleSaveHpcConnection} className="btn" disabled={saving}>
              Save HPC connection
            </button>
          </div>
        </>
      )}

      {current && (
        <p className="current">
          Current: <strong>{current.device_name}</strong>
          {executionMode === "local" && simulatedQuantum && " (simulated quantum)"}
        </p>
      )}

      <div className="form">
        <h3>HPC to Quantum data transfer</h3>
        <p className="muted">Configure how data flows between classical HPC nodes and QPUs.</p>
        <label>
          <span>Transfer batch size</span>
          <input
            type="number"
            min={1}
            value={transferBatchSize}
            onChange={(e) => setTransferBatchSize(Number(e.target.value) || 64)}
          />
        </label>
        <label>
          <span>Serialization format</span>
          <select
            value={serializationFormat}
            onChange={(e) => setSerializationFormat(e.target.value)}
          >
            <option value="json">JSON</option>
            <option value="msgpack">Msgpack</option>
          </select>
        </label>
        <label>
          <span>Polling interval (seconds)</span>
          <input
            type="number"
            min={0.1}
            step={0.1}
            value={pollingInterval}
            onChange={(e) => setPollingInterval(Number(e.target.value) || 2)}
          />
        </label>
        <button type="button" onClick={handleSaveTransferConfig} className="btn" disabled={transferSaving}>
          {transferSaving ? "Saving…" : "Save transfer config"}
        </button>
      </div>
    </section>
  );
}
