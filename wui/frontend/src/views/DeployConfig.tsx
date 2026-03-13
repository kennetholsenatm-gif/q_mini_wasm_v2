import { useState } from "react";
import { deployApi } from "../api/client";

export function DeployConfig() {
  const [providerId, setProviderId] = useState("lambda");
  const [instanceType, setInstanceType] = useState("");
  const [region, setRegion] = useState("");
  const [result, setResult] = useState<{ job: string; status: string; config_path?: string } | null>(null);
  const [loading, setLoading] = useState(false);

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
