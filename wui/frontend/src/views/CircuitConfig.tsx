import { useEffect, useState } from "react";
import { circuitsApi, type CircuitCurrent } from "../api/client";

export function CircuitConfig() {
  const [current, setCurrent] = useState<CircuitCurrent | null>(null);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [form, setForm] = useState({ num_qubits: 8, qaoa_layers: 3, diff_method: "parameter-shift" });

  useEffect(() => {
    circuitsApi.getCurrent()
      .then((c) => {
        setCurrent(c);
        setForm({ num_qubits: c.num_qubits, qaoa_layers: c.qaoa_layers, diff_method: c.diff_method });
      })
      .finally(() => setLoading(false));
  }, []);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setSaving(true);
    try {
      const updated = await circuitsApi.setCurrent(form);
      setCurrent(updated);
    } finally {
      setSaving(false);
    }
  };

  if (loading) return <section className="panel">Loading circuit config…</section>;
  return (
    <section className="panel">
      <h2>Circuit (PQC) Selection</h2>
      <p className="muted">Configure the Parameterized Quantum Circuit (QAOA layers, num_qubits) for the quantum router.</p>
      <form onSubmit={handleSubmit} className="form">
        <label>
          <span>Num qubits</span>
          <input
            type="number"
            min={1}
            max={32}
            value={form.num_qubits}
            onChange={(e) => setForm((f) => ({ ...f, num_qubits: parseInt(e.target.value, 10) || 8 }))}
          />
        </label>
        <label>
          <span>QAOA layers</span>
          <input
            type="number"
            min={1}
            max={20}
            value={form.qaoa_layers}
            onChange={(e) => setForm((f) => ({ ...f, qaoa_layers: parseInt(e.target.value, 10) || 3 }))}
          />
        </label>
        <label>
          <span>Diff method</span>
          <select
            value={form.diff_method}
            onChange={(e) => setForm((f) => ({ ...f, diff_method: e.target.value }))}
          >
            <option value="parameter-shift">parameter-shift</option>
            <option value="finite-diff">finite-diff</option>
          </select>
        </label>
        <button type="submit" disabled={saving} className="btn">
          Save
        </button>
      </form>
      {current && (
        <p className="current">
          Current: {current.num_qubits} qubits, {current.qaoa_layers} QAOA layers, {current.diff_method}
        </p>
      )}
    </section>
  );
}
