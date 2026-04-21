import React, { useState, useEffect } from 'react'

function InferencePanel() {
  const [prompt, setPrompt] = useState('')
  const [history, setHistory] = useState([])
  const [isLoading, setIsLoading] = useState(false)
  const [models, setModels] = useState([])
  const [selectedModel, setSelectedModel] = useState(null)
  const [params, setParams] = useState({
    temperature: 0.7,
    maxTokens: 512,
    topP: 0.9
  })
  const [efficiency, setEfficiency] = useState(null)

  useEffect(() => {
    loadModels()
    fetchEfficiency()
  }, [])

  const fetchEfficiency = async () => {
    try {
      const res = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_get_ops_snapshot',
          params: {}
        })
      })
      const data = await res.json()
      if (data.result?.efficiency?.energy_percent) {
        setEfficiency(data.result.efficiency.energy_percent)
      }
    } catch (err) {
      console.error('Failed to fetch efficiency:', err)
    }
  }

  const loadModels = async () => {
    try {
      const res = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_list_models',
          params: {}
        })
      })
      const data = await res.json()
      if (data.result?.models) {
        setModels(data.result.models)
        if (data.result.models.length > 0) {
          setSelectedModel(data.result.models[0])
        }
      }
    } catch (err) {
      console.error('Failed to load models:', err)
    }
  }

  const handleSubmit = async (e) => {
    e.preventDefault()
    if (!prompt.trim() || isLoading || !selectedModel) return

    setIsLoading(true)
    const userMessage = { role: 'user', content: prompt }
    setHistory(prev => [...prev, userMessage])

    try {
      const res = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_run_inference',
          params: {
            prompt: prompt,
            model: selectedModel.id,
            temperature: params.temperature,
            max_tokens: params.maxTokens,
            top_p: params.topP
          },
        }),
      })
      const data = await res.json()
      const aiResponse = data.result?.response || 'Error running inference'
      
      setHistory(prev => [...prev, { role: 'assistant', content: aiResponse }])
    } catch (err) {
      setHistory(prev => [...prev, { role: 'assistant', content: 'Error: ' + err.message }])
    }
    setIsLoading(false)
    setPrompt('')
  }

  const clearChat = () => setHistory([])

  const formatSize = (bytes) => {
    if (bytes === 0) return '0 B'
    const k = 1024
    const sizes = ['B', 'KB', 'MB', 'GB']
    const i = Math.floor(Math.log(bytes) / Math.log(k))
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
  }

  return (
    <div className="flex gap-4 h-[calc(100vh-100px)]">
      {/* Left Panel - Model Selection */}
      <div className="w-64 flex flex-col gap-4">
        {/* Efficiency Banner */}
        <div className="card" style={{background: 'linear-gradient(135deg, #1a3d2e 0%, #2d5a44 100%)', border: '1px solid #4ecca3'}}>
          <div className="text-center">
            <div className="text-2xl font-bold text-green-400">{efficiency ? `${efficiency}%` : 'N/A'}</div>
            <div className="text-xs text-gray-300">Energy Efficiency</div>
          </div>
        </div>

        {/* Model List */}
        <div className="card flex-1 flex flex-col">
          <h3 className="text-sm font-semibold uppercase tracking-wide text-green-400 mb-3">Models</h3>
          <div className="flex-1 overflow-y-auto space-y-1 bg-dark-800 rounded p-2">
            {models.length === 0 ? (
              <p className="text-sm text-gray-500 p-2">No models loaded</p>
            ) : (
              models.map((model) => (
                <div
                  key={model.id}
                  onClick={() => setSelectedModel(model)}
                  className={`p-2 rounded cursor-pointer text-sm flex justify-between items-center ${
                    selectedModel?.id === model.id
                      ? 'bg-green-900/50 border-l-2 border-green-400'
                      : 'hover:bg-dark-700'
                  }`}
                >
                  <span>{model.name}</span>
                  <span className="text-xs text-gray-400">{formatSize(model.size)}</span>
                </div>
              ))
            )}
          </div>
          <button onClick={loadModels} className="btn-secondary mt-3 w-full text-sm">Refresh Models</button>
        </div>
      </div>

      {/* Center - Chat Area */}
      <div className="flex-1 flex flex-col gap-4">
        <div className="card flex-1 flex flex-col min-h-0">
          <div className="flex items-center justify-between mb-3">
            <h3 className="text-lg font-semibold">Chat</h3>
            <button onClick={clearChat} className="text-sm text-red-400 hover:text-red-300">Clear</button>
          </div>
          
          <div className="flex-1 overflow-y-auto bg-dark-800 rounded-lg p-4 space-y-4">
            {history.length === 0 ? (
              <p className="text-gray-500 text-center mt-8">
                {selectedModel ? `Chat with ${selectedModel.name}` : 'Select a model to start chatting'}
              </p>
            ) : (
              history.map((msg, i) => (
                <div
                  key={i}
                  className={`max-w-[85%] ${msg.role === 'user' ? 'ml-auto' : 'mr-auto'}`}
                >
                  <div
                    className={`p-3 rounded-xl ${
                      msg.role === 'user'
                        ? 'bg-green-800/50 rounded-br-none'
                        : 'bg-dark-700 border border-dark-600 rounded-bl-none'
                    }`}
                  >
                    <div className="text-xs text-gray-400 mb-1">{msg.role === 'user' ? 'You' : 'AI'}</div>
                    <div className="whitespace-pre-wrap">{msg.content}</div>
                  </div>
                </div>
              ))
            )}
          </div>

          <form onSubmit={handleSubmit} className="mt-3">
            <div className="flex gap-2">
              <textarea
                value={prompt}
                onChange={(e) => setPrompt(e.target.value)}
                onKeyDown={(e) => {
                  if (e.key === 'Enter' && !e.shiftKey) {
                    e.preventDefault()
                    handleSubmit(e)
                  }
                }}
                placeholder={selectedModel ? "Enter your prompt..." : "Select a model first..."}
                className="flex-1 bg-dark-800 border border-dark-700 rounded-lg px-4 py-3 text-white placeholder-gray-500 focus:outline-none focus:ring-2 focus:ring-green-500 resize-none"
                rows={3}
                disabled={!selectedModel || isLoading}
              />
              <button
                type="submit"
                disabled={!selectedModel || isLoading}
                className="px-6 bg-green-600 hover:bg-green-500 disabled:bg-gray-600 text-white rounded-lg font-medium transition-colors"
              >
                {isLoading ? '...' : 'Send'}
              </button>
            </div>
          </form>
        </div>
      </div>

      {/* Right Panel - Parameters */}
      <div className="w-72 space-y-4">
        <div className="card">
          <h3 className="text-sm font-semibold uppercase tracking-wide text-green-400 mb-4">Parameters</h3>
          
          <div className="space-y-4">
            <div>
              <div className="flex justify-between text-xs mb-1">
                <span className="text-gray-400">Temperature</span>
                <span className="text-green-400">{params.temperature.toFixed(2)}</span>
              </div>
              <input
                type="range"
                min="0"
                max="2"
                step="0.05"
                value={params.temperature}
                onChange={(e) => setParams({ ...params, temperature: parseFloat(e.target.value) })}
                className="w-full h-1 bg-dark-700 rounded-lg appearance-none cursor-pointer"
                style={{accentColor: '#4ecca3'}}
              />
            </div>

            <div>
              <div className="flex justify-between text-xs mb-1">
                <span className="text-gray-400">Max Tokens</span>
                <span className="text-green-400">{params.maxTokens}</span>
              </div>
              <input
                type="range"
                min="64"
                max="2048"
                step="64"
                value={params.maxTokens}
                onChange={(e) => setParams({ ...params, maxTokens: parseInt(e.target.value) })}
                className="w-full h-1 bg-dark-700 rounded-lg appearance-none cursor-pointer"
              />
            </div>

            <div>
              <div className="flex justify-between text-xs mb-1">
                <span className="text-gray-400">Top P</span>
                <span className="text-green-400">{params.topP.toFixed(2)}</span>
              </div>
              <input
                type="range"
                min="0"
                max="1"
                step="0.05"
                value={params.topP}
                onChange={(e) => setParams({ ...params, topP: parseFloat(e.target.value) })}
                className="w-full h-1 bg-dark-700 rounded-lg appearance-none cursor-pointer"
              />
            </div>
          </div>
        </div>

        {selectedModel && (
          <div className="card">
            <h3 className="text-sm font-semibold uppercase tracking-wide text-green-400 mb-2">Selected Model</h3>
            <div className="text-sm text-gray-300">{selectedModel.name}</div>
            <div className="text-xs text-gray-400 mt-1">{formatSize(selectedModel.size)}</div>
          </div>
        )}
      </div>
    </div>
  )
}

export default InferencePanel
