import { HardwareConfig } from "./views/HardwareConfig";
import { QuantumBackendConfig } from "./views/QuantumBackendConfig";
import { CircuitConfig } from "./views/CircuitConfig";
import { DeployConfig } from "./views/DeployConfig";

function App() {
  return (
    <div className="app">
      <header>
        <h1>Q-Mini-WASM WUI</h1>
        <p className="tagline">Deployment control plane for Cloud HPC and quantum backend configuration</p>
      </header>
      <main>
        <HardwareConfig />
        <QuantumBackendConfig />
        <CircuitConfig />
        <DeployConfig />
      </main>
    </div>
  );
}

export default App;
