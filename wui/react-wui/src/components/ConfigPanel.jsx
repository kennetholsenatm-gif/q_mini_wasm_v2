import React, { useState, useEffect } from 'react'

function ConfigPanel() {
  const [tomlConfig, setTomlConfig] = useState('')
  const [apiKeys, setApiKeys] = useState({
    wolfram: '',
    wikidata: '',
    arxiv: '',
    github: '',
    lean: ''
  })
  const [status, setStatus] = useState('')
  const [dataSources, setDataSources] = useState([])
  const [showSourceLibrary, setShowSourceLibrary] = useState(false)

  useEffect(() => {
    loadConfig()
    loadApiKeys()
    loadDataSources()
  }, [])

  // Real default config from default_training_config.toml
  const defaultConfig = `# Q-Mini-WASM-v2 Training Configuration
# This is the comprehensive default configuration
# ALL settings must be defined here - no hardcoded defaults in code

[training]
epochs = 100
batch_size = 32768
checkpoint_interval = 10
learning_rate = 10.0
resume = false

[model]
moe_experts = 8192
moe_top_k = 128
context_window = 8192
entanglement_tokens = 1024
shadow_dim = 4096
num_layers = 64
neurons_per_layer = 8192
routing_qutrits = 12
learning_rate_shift = 2

[features]
steane_correction = true
cim_mode = false
worker_threads = 32
continuous_mode = true
lazy_init = true
data_accumulation = true
lazy_provisioning = true
infinite_data = true

[network]
websocket_port = 8080
realtime_metrics = true
theme = "dark"

[paths]
output_dir = "C:/q_mini_data/checkpoints/"
checkpoint_dir = "C:/q_mini_data/checkpoints/"
dataset_dir = "C:/q_mini_data/datasets/"
base_model = "C:/q_mini_data/checkpoints/final_model.json"

[streaming]
port = 9090
sse_port = 9091

[apis]
enabled = true
pubchem = true
oeis = true
nasa = true
pdb = true
openalex = true

[memory]
arena_mb = 4096
max_memory_gb = 48

[som]
initial_experts = 2
split_beta1_threshold = 5
target_graph_density = 0.15
max_experts_hard_cap = 100000
min_graph_density = 0.05
max_graph_density = 0.40
merge_min_activation_rate = 0.001

[system.inference]
num_qutrits = 243
num_experts = 243
model_size_b = 100
goodness_threshold = 5.0
betti_analysis = true

[training.data_filter]
min_text_length = 50
max_text_length = 100000

[model.expert]
sparsity = 5`

  const loadConfig = async () => {
    setStatus('Loading config...')
    try {
      const res = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_load_config',
          params: {}
        })
      })
      const data = await res.json()
      if (data.result?.content) {
        setTomlConfig(data.result.content)
        setStatus('Config loaded')
      } else {
        setTomlConfig(defaultConfig)
        setStatus('Using default config')
      }
      setTimeout(() => setStatus(''), 2000)
    } catch (err) {
      console.error('Failed to load config:', err)
      setTomlConfig(defaultConfig)
      setStatus('Using default (load failed)')
      setTimeout(() => setStatus(''), 2000)
    }
  }

  const saveConfig = async () => {
    setStatus('Saving config...')
    try {
      const res = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_save_config',
          params: { content: tomlConfig }
        })
      })
      setStatus('Config saved!')
      setTimeout(() => setStatus(''), 2000)
    } catch (err) {
      setStatus('Error: ' + err.message)
    }
  }

  const resetConfig = async () => {
    if (!confirm('Reset to default configuration?')) return
    try {
      const res = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_reset_config',
          params: {}
        })
      })
      const data = await res.json()
      if (data.result?.config) {
        setTomlConfig(data.result.config)
      }
    } catch (err) {
      console.error('Failed to reset config:', err)
    }
  }

  const loadApiKeys = async () => {
    try {
      const res = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_get_api_keys',
          params: {}
        })
      })
      const data = await res.json()
      if (data.result) {
        setApiKeys(data.result)
      }
    } catch (err) {
      console.error('Failed to load API keys:', err)
    }
  }

  const saveApiKeys = async () => {
    setStatus('Saving API keys...')
    try {
      await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_save_api_keys',
          params: apiKeys
        })
      })
      setStatus('API keys saved!')
      setTimeout(() => setStatus(''), 2000)
    } catch (err) {
      setStatus('Error: ' + err.message)
    }
  }

  const loadDataSources = async () => {
    try {
      const res = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_get_data_sources',
          params: {}
        })
      })
      const data = await res.json()
      if (data.result?.sources) {
        setDataSources(data.result.sources)
      }
    } catch (err) {
      console.error('Failed to load data sources:', err)
    }
  }

  const addSourceToConfig = async () => {
    try {
      await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_add_sources_to_config',
          params: { sources: dataSources }
        })
      })
      setStatus('Sources added to config!')
      setTimeout(() => setStatus(''), 2000)
    } catch (err) {
      console.error('Failed to add sources:', err)
    }
  }

  const getKeyStatus = (key) => key ? 'Saved' : 'Missing'
  const getKeyStatusClass = (key) => key ? 'text-green-400' : 'text-red-400'

  return (
    <div className="space-y-4 max-w-6xl">
      <h2 className="text-2xl font-bold">Training Configuration</h2>
      
      {status && <div className="text-sm text-primary-400">{status}</div>}

      {/* TOML Editor */}
      <div className="card">
        <h3 className="text-lg font-semibold mb-3">TOML Configuration</h3>
        <textarea
          value={tomlConfig}
          onChange={(e) => setTomlConfig(e.target.value)}
          className="w-full h-96 font-mono text-sm bg-dark-900 border border-dark-700 rounded-lg p-4 text-white resize-y focus:outline-none focus:ring-2 focus:ring-primary-500"
          spellCheck={false}
        />
        <div className="flex gap-3 mt-4">
          <button onClick={saveConfig} className="btn-primary">Save Config</button>
          <button onClick={loadConfig} className="btn-secondary">Reload</button>
          <button onClick={resetConfig} className="btn-secondary">Reset to Default</button>
        </div>
      </div>

      {/* API Credentials */}
      <div className="card">
        <h3 className="text-lg font-semibold mb-3">API Credentials</h3>
        <div className="space-y-3">
          {[
            { key: 'wolfram', label: 'WolframAlpha' },
            { key: 'wikidata', label: 'Wikidata' },
            { key: 'arxiv', label: 'arXiv (optional)' },
            { key: 'github', label: 'GitHub' },
            { key: 'lean', label: 'Lean' },
          ].map(({ key, label }) => (
            <div key={key} className="flex items-center gap-3">
              <label className="w-32 text-sm text-gray-400">{label}:</label>
              <input
                type="password"
                value={apiKeys[key] || ''}
                onChange={(e) => setApiKeys({ ...apiKeys, [key]: e.target.value })}
                placeholder={`${label} API Key`}
                className="flex-1 bg-dark-800 border border-dark-700 rounded px-3 py-2 text-sm text-white placeholder-gray-500 focus:outline-none focus:ring-2 focus:ring-primary-500"
              />
              <span className={`text-sm ${getKeyStatusClass(apiKeys[key])}`}>
                {getKeyStatus(apiKeys[key])}
              </span>
            </div>
          ))}
        </div>
        <div className="flex gap-3 mt-4">
          <button onClick={saveApiKeys} className="btn-primary">Save API Keys</button>
          <button onClick={loadApiKeys} className="btn-secondary">Reload</button>
        </div>
      </div>

      {/* Data Sources */}
      <div className="card">
        <div className="flex items-center justify-between mb-3">
          <h3 className="text-lg font-semibold">Data Sources ({dataSources.length} configured)</h3>
          <div className="flex gap-2">
            <button onClick={() => setShowSourceLibrary(true)} className="btn-primary">Source Library</button>
            <button onClick={addSourceToConfig} className="btn-secondary" style={{background: '#2d7a51'}}>Add to Config</button>
          </div>
        </div>
        <div className="bg-dark-800 rounded-lg p-3 max-h-48 overflow-y-auto">
          {dataSources.length === 0 ? (
            <p className="text-gray-500 text-sm">No data sources configured</p>
          ) : (
            <div className="space-y-2">
              {dataSources.map((source, i) => (
                <div key={i} className="flex items-center justify-between bg-dark-700 rounded px-3 py-2">
                  <span className="text-sm">{source.name || source.url}</span>
                  <span className="text-xs text-gray-400">{source.type}</span>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>

      {/* Source Library Modal */}
      {showSourceLibrary && (
        <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
          <div className="bg-dark-900 rounded-xl p-6 w-full max-w-2xl max-h-[80vh] overflow-y-auto">
            <div className="flex items-center justify-between mb-4">
              <h3 className="text-xl font-semibold">Data Source Library</h3>
              <button onClick={() => setShowSourceLibrary(false)} className="text-2xl text-gray-400 hover:text-white">&times;</button>
            </div>
            <div className="grid grid-cols-2 gap-3">
              {['All Sources', 'Math/CS', 'Science', 'Bio', 'Knowledge', 'ML/AI'].map((cat) => (
                <button key={cat} className="p-3 bg-dark-800 hover:bg-dark-700 rounded-lg text-left">
                  <div className="font-medium">{cat}</div>
                  <div className="text-sm text-gray-400">Available sources</div>
                </button>
              ))}
            </div>
          </div>
        </div>
      )}

      {/* Data Paths */}
      <div className="card">
        <h3 className="text-lg font-semibold mb-3">Data Paths</h3>
        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 text-sm">
          <div>
            <div className="text-gray-400">Config</div>
            <code className="text-xs bg-dark-800 px-2 py-1 rounded block mt-1">C:\q_mini_data\config\training_config.toml</code>
          </div>
          <div>
            <div className="text-gray-400">Datasets</div>
            <code className="text-xs bg-dark-800 px-2 py-1 rounded block mt-1">C:\q_mini_data\datasets</code>
          </div>
          <div>
            <div className="text-gray-400">Models</div>
            <code className="text-xs bg-dark-800 px-2 py-1 rounded block mt-1">C:\q_mini_data\models</code>
          </div>
        </div>
      </div>
    </div>
  )
}

export default ConfigPanel
