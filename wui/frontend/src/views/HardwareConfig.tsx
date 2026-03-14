import { useEffect, useState } from "react";
import { hardwareApi, type HardwareCurrent, type HardwareOption } from "../api/client";

export function HardwareConfig() {
  const [options, setOptions] = useState<HardwareOption[]>([]);
  const [current, setCurrent] = useState<HardwareCurrent | null>(null);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    Promise.all([hardwareApi.getOptions(), hardwareApi.getCurrent()])
      .then(([opts, cur]) => {
        setOptions(opts);
        setCurrent(cur);
      })
      .finally(() => setLoading(false));
  }, []);

  const handleSelect = async (accelerator: "cuda" | "xpu" | "cpu") => {
    if (!current) return;
    setSaving(true);
    try {
      const updated = await hardwareApi.setCurrent({ accelerator, device_index: current.device_index });
      setCurrent(updated);
    } finally {
      setSaving(false);
    }
  };

  if (loading) return <section className="panel">Loading hardware options…</section>;
  return (
    <section className="panel">
      <h2>Hardware (HPC Accelerator)</h2>
      <p className="muted">Select the accelerator for the cloud instance.</p>
      <div className="option-list">
        {options.map((opt) => (
          <label key={opt.id} className="option">
            <input
              type="radio"
              name="accelerator"
              checked={current?.accelerator === opt.id}
              onChange={() => handleSelect(opt.id as "cuda" | "xpu" | "cpu")}
              disabled={saving}
            />
            <span>{opt.name}</span>
          </label>
        ))}
      </div>
      {current && (
        <p className="current">
          Current: <strong>{current.device_name}</strong>
        </p>
      )}
    </section>
  );
}
