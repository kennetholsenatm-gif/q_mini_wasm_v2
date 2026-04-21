import React, { useState, useEffect } from 'react'

function DataAcquisition({ isTraining }) {
  const [isAcquiring, setIsAcquiring] = useState(false)
  const [progress, setProgress] = useState({
    totalSources: 0,
    completedSources: 0,
    processedItems: 0,
  })
  const [dataSources, setDataSources] = useState([])
  const [selectedSource, setSelectedSource] = useState(null)
  const [localDataDir, setLocalDataDir] = useState('C:/q_mini_data/datasets')
  const [status, setStatus] = useState('')

  // Load local datasources on mount
  useEffect(() => {
    loadLocalDataSources()
  }, [])

  const loadLocalDataSources = async () => {
    try {
      const response = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_list_data_sources',
          params: {},
        }),
      })
      const data = await response.json()
      if (data.result?.sources) {
        setDataSources(data.result.sources)
        setProgress(prev => ({ ...prev, totalSources: data.result.sources.length }))
      }
    } catch (err) {
      console.error('Failed to load data sources:', err)
      setStatus('Failed to load local datasources')
    }
  }

  const handleStart = async () => {
    if (isTraining) {
      setStatus('Cannot start: Training is currently running')
      return
    }
    if (!selectedSource && dataSources.length === 0) {
      setStatus('No local datasources available')
      return
    }
    
    setIsAcquiring(true)
    setStatus('Starting data acquisition from local sources...')
    try {
      const response = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_start_data_acquisition',
          params: { 
            source_dir: selectedSource || localDataDir,
            output_dir: 'datasets/acquired' 
          },
        }),
      })
      const data = await response.json()
      if (data.result?.started) {
        setStatus('Data acquisition started')
      } else {
        setStatus('Error: ' + (data.error?.message || 'Unknown error'))
        setIsAcquiring(false)
      }
    } catch (err) {
      console.error('Failed to start acquisition:', err)
      setStatus('Failed to start: ' + err.message)
      setIsAcquiring(false)
    }
  }

  const handleStop = async () => {
    try {
      await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_stop_data_acquisition',
          params: {},
        }),
      })
      setIsAcquiring(false)
      setStatus('Stopped')
    } catch (err) {
      console.error('Failed to stop acquisition:', err)
    }
  }

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-2xl font-bold">Data Acquisition</h2>
        <div className="flex items-center gap-4">
          {status && (
            <span className={`text-sm ${
              status.includes('Error') ? 'text-red-400' : 
              status.includes('Cannot') ? 'text-yellow-400' :
              status.includes('started') ? 'text-green-400' : 
              'text-primary-400'
            }`}>{status}</span>
          )}
          <div className="flex gap-3">
            {!isAcquiring ? (
              <button 
                onClick={handleStart} 
                className={`btn-primary ${isTraining ? 'opacity-50 cursor-not-allowed' : ''}`}
                disabled={isTraining}
              >
                Start Acquisition
              </button>
            ) : (
              <button onClick={handleStop} className="btn-secondary">
                Stop
              </button>
            )}
          </div>
        </div>
      </div>

      {isTraining && (
        <div className="card border-yellow-500/50 bg-yellow-500/10">
          <div className="flex items-center gap-2 text-yellow-400">
            <span>⚠️</span>
            <span>Training is currently running. Stop training before starting data acquisition.</span>
          </div>
        </div>
      )}

      <div className="card">
        <h3 className="text-lg font-semibold mb-4">Local Data Sources</h3>
        {dataSources.length === 0 ? (
          <div className="text-gray-400">
            <p>No local datasources found in: {localDataDir}</p>
            <p className="text-sm mt-2">Add JSONL files to this directory to make them available.</p>
          </div>
        ) : (
          <div className="space-y-2">
            <p className="text-sm text-gray-400 mb-3">Found {dataSources.length} datasource(s):</p>
            {dataSources.map((source, index) => (
              <div
                key={index}
                onClick={() => setSelectedSource(source.path)}
                className={`p-3 rounded-lg cursor-pointer transition-colors ${
                  selectedSource === source.path
                    ? 'border-2 border-primary-500 bg-primary-500/10'
                    : 'border border-dark-700 hover:border-dark-600'
                }`}
              >
                <div className="flex items-center justify-between">
                  <div>
                    <div className="font-medium">{source.name}</div>
                    <div className="text-sm text-gray-400">{source.path}</div>
                  </div>
                  <div className="text-sm text-gray-500">
                    {(source.size / 1024 / 1024).toFixed(1)} MB
                  </div>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>

      <div className="card">
        <h3 className="text-lg font-semibold mb-4">Progress</h3>
        <div className="grid grid-cols-3 gap-4">
          <div className="text-center">
            <div className="text-3xl font-bold text-primary-400">
              {progress.completedSources}
            </div>
            <div className="text-sm text-gray-400">Sources Processed</div>
          </div>
          <div className="text-center">
            <div className="text-3xl font-bold text-blue-400">
              {(progress.processedItems || 0).toLocaleString()}
            </div>
            <div className="text-sm text-gray-400">Items Processed</div>
          </div>
          <div className="text-center">
            <div className="text-3xl font-bold text-green-400">
              {isAcquiring ? 'Running' : 'Idle'}
            </div>
            <div className="text-sm text-gray-400">Status</div>
          </div>
        </div>
      </div>
    </div>
  )
}

export default DataAcquisition
