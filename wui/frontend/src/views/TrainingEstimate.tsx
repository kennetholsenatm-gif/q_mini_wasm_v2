import { useCallback, useEffect, useState } from "react";
import { trainingApi, type TrainingEstimateResponse } from "../api/client";

export function TrainingEstimate() {
  const [estimate, setEstimate] = useState<TrainingEstimateResponse | null>(null);
  const [loading, setLoading] = useState(true);

  const fetchEstimate = useCallback(() => {
    setLoading(true);
    trainingApi
      .getEstimate()
      .then(setEstimate)
      .catch(() => setEstimate(null))
      .finally(() => setLoading(false));
  }, []);

  useEffect(() => {
    fetchEstimate();
  }, [fetchEstimate]);

  if (loading && !estimate) return <section className="panel">Loading estimate…</section>;
  if (!estimate) return null;
  return (
    <section className="panel">
      <h2>Training time estimate</h2>
      <p className="muted">Based on current hardware, quantum backend, and circuit settings.</p>
      <p className="current">
        Estimated training time: <strong>{estimate.message ?? `~${Math.round(estimate.estimated_seconds)} sec`}</strong>
      </p>
      <button type="button" onClick={fetchEstimate} className="btn" disabled={loading}>
        {loading ? "Refreshing…" : "Refresh estimate"}
      </button>
    </section>
  );
}
