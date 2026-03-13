const BASE = "";

async function request<T>(path: string, options?: RequestInit): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    headers: { "Content-Type": "application/json", ...options?.headers },
    ...options,
  });
  if (!res.ok) throw new Error(await res.text());
  return res.json() as Promise<T>;
}

export interface HardwareOption {
  id: string;
  name: string;
}

export interface HardwareCurrent {
  accelerator: string;
  device_index: number;
  device_name: string;
}

export interface HardwareCurrentUpdate {
  accelerator: "cuda" | "xpu" | "cpu";
  device_index?: number;
}

export const hardwareApi = {
  getOptions: () => request<HardwareOption[]>("/api/hardware/options"),
  getCurrent: () => request<HardwareCurrent>("/api/hardware/current"),
  setCurrent: (body: HardwareCurrentUpdate) =>
    request<HardwareCurrent>("/api/hardware/current", { method: "PUT", body: JSON.stringify(body) }),
};

export interface QuantumBackendInfo {
  id: string;
  name: string;
  required_env_vars: string[];
}

export interface QuantumConfig {
  backend: string;
  simulator_name?: string;
  credentials_configured?: Record<string, boolean>;
}

export interface QuantumConfigUpdate {
  backend: string;
  credentials?: Record<string, string>;
}

export const quantumApi = {
  getBackends: () => request<QuantumBackendInfo[]>("/api/quantum/backends"),
  getConfig: () => request<QuantumConfig>("/api/quantum/config"),
  setConfig: (body: QuantumConfigUpdate) =>
    request<QuantumConfig>("/api/quantum/config", { method: "PUT", body: JSON.stringify(body) }),
  verify: () => request<{ ok: boolean; message?: string }>("/api/quantum/verify", { method: "POST" }),
};

export interface CircuitPreset {
  num_qubits: number;
  qaoa_layers: number;
  diff_method: string;
}

export interface CircuitCurrent {
  num_qubits: number;
  qaoa_layers: number;
  diff_method: string;
}

export const circuitsApi = {
  getPresets: () => request<CircuitPreset[]>("/api/circuits/presets"),
  getCurrent: () => request<CircuitCurrent>("/api/circuits/current"),
  setCurrent: (body: Partial<CircuitCurrent>) =>
    request<CircuitCurrent>("/api/circuits/current", { method: "PUT", body: JSON.stringify(body) }),
};

export interface DeployDesiredRequest {
  provider_id: string;
  instance_type?: string;
  region?: string;
  metadata?: Record<string, unknown>;
}

export interface DeployDesiredResponse {
  job: string;
  status: string;
  config_path?: string;
}

export interface TfvarPreset {
  id: string;
  label: string;
  provider_id: string;
  instance_type?: string;
  region?: string;
}

export interface DeployTfvarsResponse {
  files: string[];
  presets: TfvarPreset[];
}

export const deployApi = {
  getTfvars: () => request<DeployTfvarsResponse>("/api/deploy/tfvars"),
  postDesired: (body: DeployDesiredRequest) =>
    request<DeployDesiredResponse>("/api/deploy/desired", { method: "POST", body: JSON.stringify(body) }),
};

export interface JobConfig {
  accelerator: string;
  device_index: number;
  quantum_backend: string;
  num_qubits: number;
  qaoa_layers: number;
  diff_method: string;
  epochs: number;
  batch_size: number;
}

export const jobConfigApi = {
  getJobConfig: () => request<JobConfig>("/api/job-config"),
};

export interface TrainingEstimateResponse {
  estimated_seconds: number;
  message?: string;
}

export const trainingApi = {
  getEstimate: (params?: {
    accelerator?: string;
    quantum_backend?: string;
    num_qubits?: number;
    qaoa_layers?: number;
    epochs?: number;
    batch_size?: number;
  }) => {
    const search = params ? new URLSearchParams(params as Record<string, string>).toString() : "";
    return request<TrainingEstimateResponse>(`/api/training/estimate${search ? `?${search}` : ""}`);
  },
};
