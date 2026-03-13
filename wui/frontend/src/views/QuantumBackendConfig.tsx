import { useEffect, useState } from "react";
import { quantumApi, type QuantumBackendInfo, type QuantumConfig } from "../api/client";

export function QuantumBackendConfig() {
  const [backends, setBackends] = useState<QuantumBackendInfo[]>([]);
  const [config, setConfig] = useState<QuantumConfig | null>(null);
  const [verifyResult, setVerifyResult] = useState<{ ok: boolean; message?: string } | null>(null);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    Promise.all([quantumApi.getBackends(), quantumApi.getConfig()])
      .then(([b, c]) => {
        setBackends(b);
        setConfig(c);
      })
      .finally(() => setLoading(false));
  }, []);

  const handleSelect = async (backendId: string) => {
    setSaving(true);
    setVerifyResult(null);
    try {
      const updated = await quantumApi.setConfig({ backend: backendId, credentials: {} });
      setConfig(updated);
    } finally {
      setSaving(false);
    }
  };

  const handleVerify = async () => {
    setVerifyResult(null);
    try {
      const result = await quantumApi.verify();
      setVerifyResult(result);
    } catch (e) {
      setVerifyResult({ ok: false, message: String(e) });
    }
  };

  if (loading) return <section className="panel">Loading quantum backends…</section>;
  return (
    <section className="panel">
      <h2>Quantum Backend</h2>
      <p className="muted">Select the target quantum backend and configure API credentials (e.g. from .env).</p>
      <div className="option-list">
        {backends.map((b) => (
          <label key={b.id} className="option">
            <input
              type="radio"
              name="quantum_backend"
              checked={config?.backend === b.id}
              onChange={() => handleSelect(b.id)}
              disabled={saving}
            />
            <span>{b.name}</span>
            {b.required_env_vars.length > 0 && (
              <small className="muted"> ({b.required_env_vars.join(", ")})</small>
            )}
          </label>
        ))}
      </div>
      {config && (
        <p className="current">
          Current: <strong>{config.backend}</strong>
          {config.simulator_name && ` (${config.simulator_name})`}
        </p>
      )}
      <button type="button" onClick={handleVerify} className="btn">
        Verify connection
      </button>
      {verifyResult && (
        <p className={verifyResult.ok ? "success" : "error"}>
          {verifyResult.ok ? "OK" : "Error"}: {verifyResult.message ?? ""}
        </p>
      )}
    </section>
  );
}
