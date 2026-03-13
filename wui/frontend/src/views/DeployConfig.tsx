import { useEffect, useState } from "react";
import { deployApi, type TfvarPreset } from "../api/client";

export function DeployConfig() {
  const [providerId, setProviderId] = useState("lambda");
  const [instanceType, setInstanceType] = useState("");
  const [region, setRegion] = useState("");
  const [result, setResult] = useState<{ job: string; status: string; config_path?: string } | null>(null);
  const [loading, setLoading] = useState(false);
  const [presets, setPresets] = useState<TfvarPreset[]>([]);
  const [files, setFiles] = useState<string[]>([]);
  const [selectedPresetId, setSelectedPresetId] = useState<string>("");
  const [selectedFile, setSelectedFile] = useState<string>("");

  useEffect(() => {
    deployApi.getTfvars().then((r) => {
      setPresets(r.presets);
      setFiles(r.files);
      if (r.presets.length > 0) {
        setSelectedPresetId(r.presets[0].id);
        const p = r.presets[0];
        setProviderId(p.provider_id);
        setInstanceType(p.instance_type ?? "");
        setRegion(p.region ?? "");
      }
    });
  }, []);

  const handlePresetChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
    const id = e.target.value;
    setSelectedPresetId(id);
    const preset = presets.find((p) => p.id === id);
    if (preset) {
      setProviderId(preset.provider_id);
      setInstanceType(preset.instance_type ?? "");
      setRegion(preset.region ?? "");
    }
  };

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setLoading(true);
    setResult(null);
    try {
      const res = await deployApi.postDesired({
        provider_id: providerId,
        instance_type: instanceType || undefined,
        region: region || undefined,
      });
      setResult(res);
      deployApi.getTfvars().then((r) => setFiles(r.files));
    } catch (err) {
      setResult({ job: "opentofu", status: "error", config_path: String(err) });
    } finally {
      setLoading(false);
    }
  };

  return (
    <section className="panel">
      <h2>Deployment (OpenTofu)</h2>
      <p className="muted">Write desired state to infra/opentofu/desired/; CI runs opentofu apply on push.</p>
      <div className="form">
        <label>
          <span>Use preset</span>
          <select value={selectedPresetId} onChange={handlePresetChange}>
            <option value="">— Select preset —</option>
            {presets.map((p) => (
              <option key={p.id} value={p.id}>
                {p.label}
              </option>
            ))}
          </select>
        </label>
        {files.length > 0 && (
          <label>
            <span>Existing tfvars</span>
            <select
              value={selectedFile}
              onChange={(e) => setSelectedFile(e.target.value)}
            >
              <option value="">— None —</option>
              {files.map((f) => (
                <option key={f} value={f}>
                  {f}
                </option>
              ))}
            </select>
            {selectedFile && (
              <p className="muted">Config already exists. CI will apply on push.</p>
            )}
          </label>
        )}
      </div>
      <form onSubmit={handleSubmit} className="form">
        <label>
          <span>Provider ID</span>
          <input
            value={providerId}
            onChange={(e) => setProviderId(e.target.value)}
            placeholder="lambda"
          />
        </label>
        <label>
          <span>Instance type (optional)</span>
          <input
            value={instanceType}
            onChange={(e) => setInstanceType(e.target.value)}
            placeholder=""
          />
        </label>
        <label>
          <span>Region (optional)</span>
          <input
            value={region}
            onChange={(e) => setRegion(e.target.value)}
            placeholder=""
          />
        </label>
        <button type="submit" disabled={loading} className="btn">
          {loading ? "Submitting…" : "Submit desired"}
        </button>
      </form>
      {result && (
        <p className="current">
          {result.status}: {result.config_path ?? result.status}
        </p>
      )}
    </section>
  );
}
