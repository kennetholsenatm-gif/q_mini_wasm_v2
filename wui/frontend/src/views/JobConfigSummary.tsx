import { useEffect, useState } from "react";
import { jobConfigApi, type JobConfig } from "../api/client";

export function JobConfigSummary() {
  const [config, setConfig] = useState<JobConfig | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    jobConfigApi
      .getJobConfig()
      .then(setConfig)
      .finally(() => setLoading(false));
  }, []);

  if (loading || !config) return null;
  return (
    <section className="panel job-config-summary">
      <h2>Training job config</h2>
      <p className="muted">Job will use the following (inject as env on the engine).</p>
      <ul className="job-config-list">
        <li>
          <strong>Hardware:</strong> {config.accelerator} (device {config.device_index})
        </li>
        <li>
          <strong>Quantum backend:</strong> {config.quantum_backend}
        </li>
        <li>
          <strong>Circuit:</strong> {config.num_qubits} qubits, {config.qaoa_layers} QAOA layers, {config.diff_method}
        </li>
        <li>
          <strong>Training:</strong> {config.epochs} epochs, batch size {config.batch_size}
        </li>
      </ul>
    </section>
  );
}
