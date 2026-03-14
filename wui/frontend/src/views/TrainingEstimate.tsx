import { useCallback, useEffect, useState } from "react";
import {
  trainingApi,
  type TrainingConfig,
  type TrainingEstimateResponse,
} from "../api/client";

const MIN_PARAMS = 1;
const MAX_PARAMS = 100_000;
const DEFAULT_PARAMS = 1024;

export function TrainingEstimate() {
  const [trainingConfig, setTrainingConfig] = useState<TrainingConfig | null>(null);
  const [numTrainableParameters, setNumTrainableParameters] = useState(DEFAULT_PARAMS);
  const [estimate, setEstimate] = useState<TrainingEstimateResponse | null>(null);
  const [loading, setLoading] = useState(true);
  const [savingParams, setSavingParams] = useState(false);

  const fetchConfig = useCallback(() => {
    trainingApi
      .getConfig()
      .then((tc) => {
        setTrainingConfig(tc);
        setNumTrainableParameters(tc.num_trainable_parameters);
      })
      .catch(() => setNumTrainableParameters(DEFAULT_PARAMS));
  }, []);

  const fetchEstimate = useCallback((params?: { num_trainable_parameters?: number }) => {
    setLoading(true);
    trainingApi
      .getEstimate(params)
      .then(setEstimate)
      .catch(() => setEstimate(null))
      .finally(() => setLoading(false));
  }, []);

  useEffect(() => {
    fetchConfig();
  }, [fetchConfig]);

  useEffect(() => {
    fetchEstimate();
  }, [fetchEstimate]);

  const handleParameterChange = (value: number) => {
    const v = Math.min(MAX_PARAMS, Math.max(MIN_PARAMS, value));
    setNumTrainableParameters(v);
  };

  const handleSaveParameters = async () => {
    setSavingParams(true);
    try {
      const tc = await trainingApi.setConfig({ num_trainable_parameters: numTrainableParameters });
      setTrainingConfig(tc);
      fetchEstimate({ num_trainable_parameters: numTrainableParameters });
    } finally {
      setSavingParams(false);
    }
  };

  if (loading && !estimate) return <section className="panel">Loading estimate…</section>;
  return (
    <section className="panel">
      <h2>Training time estimate</h2>
      <p className="muted">Based on current hardware, quantum backend, circuit settings, and number of parameters.</p>
      <div className="form">
        <label>
          <span>Number of parameters to train</span>
          <input
            type="number"
            min={MIN_PARAMS}
            max={MAX_PARAMS}
            value={numTrainableParameters}
            onChange={(e) => handleParameterChange(Number(e.target.value) || MIN_PARAMS)}
            aria-label="Number of trainable parameters"
          />
        </label>
        <label>
          <span>Range: {MIN_PARAMS} – {MAX_PARAMS}</span>
          <input
            type="range"
            min={MIN_PARAMS}
            max={Math.min(MAX_PARAMS, 10_000)}
            value={Math.min(numTrainableParameters, 10_000)}
            onChange={(e) => handleParameterChange(Number(e.target.value))}
            aria-label="Number of trainable parameters (slider)"
          />
        </label>
        <button type="button" onClick={handleSaveParameters} className="btn" disabled={savingParams}>
          {savingParams ? "Saving…" : "Apply parameters"}
        </button>
      </div>
      {estimate && (
        <p className="current">
          Estimated training time: <strong>{estimate.message ?? `~${Math.round(estimate.estimated_seconds)} sec`}</strong>
        </p>
      )}
      <button type="button" onClick={() => fetchEstimate()} className="btn" disabled={loading}>
        {loading ? "Refreshing…" : "Refresh estimate"}
      </button>
    </section>
  );
}
