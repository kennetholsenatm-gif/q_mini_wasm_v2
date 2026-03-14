import { useEffect, useState } from "react";
import { quantumApi, type QuantumBackendInfo, type QuantumConfig } from "../api/client";

export function QuantumBackendConfig() {
  const [backends, setBackends] = useState<QuantumBackendInfo[]>([]);
  const [config, setConfig] = useState<QuantumConfig | null>(null);
  const [verifyResult, setVerifyResult] = useState<{ ok: boolean; message?: string } | null>(null);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [credSaving, setCredSaving] = useState(false);
  const [ibmApiKey, setIbmApiKey] = useState("");
  const [ibmUrl, setIbmUrl] = useState("");
  const [intelQsPython, setIntelQsPython] = useState("");
  const [intelSdkPath, setIntelSdkPath] = useState("");

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

  const handleSaveCredentials = async () => {
    if (!config) return;
    setCredSaving(true);
    try {
      const credentials: Record<string, string> = {};
      if (config.backend === "ibm_quantum") {
        if (ibmApiKey.trim()) credentials.IBM_QUANTUM_API_KEY = ibmApiKey.trim();
        if (ibmUrl.trim()) credentials.IBM_QUANTUM_URL = ibmUrl.trim();
      } else if (config.backend === "intel_qs") {
        if (intelQsPython.trim()) credentials.INTEL_QS_PYTHON = intelQsPython.trim();
        if (intelSdkPath.trim()) credentials.INTEL_QUANTUM_SDK_PATH = intelSdkPath.trim();
      }
      const updated = await quantumApi.setConfig({ backend: config.backend, credentials });
      setConfig(updated);
    } finally {
      setCredSaving(false);
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
      {config?.backend === "penny_lane" && (
        <p className="muted">
          PennyLane (default.qubit) will be used for the training job with the circuit settings below.
        </p>
      )}
      {config?.backend === "ibm_quantum" && (
        <div className="form credential-form">
          <label>
            <span>IBM Quantum API Key</span>
            <input
              type="password"
              value={ibmApiKey}
              onChange={(e) => setIbmApiKey(e.target.value)}
              placeholder="Set and save (not echoed back)"
            />
          </label>
          <label>
            <span>IBM Quantum URL (optional)</span>
            <input
              type="text"
              value={ibmUrl}
              onChange={(e) => setIbmUrl(e.target.value)}
              placeholder="https://quantum-computing.ibm.com/api"
            />
          </label>
          {config.credentials_configured && (
            <p className="muted">
              Configured:{" "}
              {Object.entries(config.credentials_configured)
                .filter(([, v]) => v)
                .map(([k]) => k)
                .join(", ") || "none"}
            </p>
          )}
          <button type="button" onClick={handleSaveCredentials} disabled={credSaving} className="btn">
            Save credentials
          </button>
        </div>
      )}
      {config?.backend === "intel_qs" && (
        <div className="form credential-form">
          <label>
            <span>INTEL_QS_PYTHON</span>
            <input
              type="text"
              value={intelQsPython}
              onChange={(e) => setIntelQsPython(e.target.value)}
              placeholder="Path or command"
            />
          </label>
          <label>
            <span>INTEL_QUANTUM_SDK_PATH</span>
            <input
              type="text"
              value={intelSdkPath}
              onChange={(e) => setIntelSdkPath(e.target.value)}
              placeholder="SDK path"
            />
          </label>
          {config.credentials_configured && (
            <p className="muted">
              Configured:{" "}
              {Object.entries(config.credentials_configured)
                .filter(([, v]) => v)
                .map(([k]) => k)
                .join(", ") || "none"}
            </p>
          )}
          <button type="button" onClick={handleSaveCredentials} disabled={credSaving} className="btn">
            Save credentials
          </button>
        </div>
      )}
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
