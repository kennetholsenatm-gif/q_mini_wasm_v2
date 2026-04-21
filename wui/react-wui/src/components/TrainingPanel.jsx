import React, { useEffect, useRef } from 'react'

function TrainingPanel({ isTraining, setIsTraining, logs, setLogs, progress, setProgress, status, setStatus }) {
  const logEndRef = useRef(null)

  useEffect(() => {
    logEndRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [logs])

  const handleStartTraining = async () => {
    setStatus('Starting training...')
    try {
      const response = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_start_training_sse',
          params: {},
        }),
      })
      const data = await response.json()
      console.log('Training start response:', data)
      
      if (data.result?.started === true) {
        setIsTraining(true)
        setStatus('Training started!')
        setLogs([{ status: 'started', message: data.result?.message || 'Training started', stream_url: data.result?.stream_url }])
      } else if (data.error) {
        setStatus('Error: ' + data.error.message)
        setLogs([{ status: 'error', message: data.error.message }])
      } else {
        setStatus('Unexpected response from server')
        console.error('Unexpected response:', data)
      }
    } catch (err) {
      setStatus('Failed to start: ' + err.message)
      console.error('Failed to start training:', err)
      console.error('Error details:', {
        message: err.message,
        name: err.name,
        stack: err.stack
      })
      setLogs([{ status: 'error', message: 'Failed to start training: ' + err.message }])
    }
  }

  const handleStopTraining = async () => {
    try {
      await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_stop_training_sse',
          params: {},
        }),
      })
      setIsTraining(false)
      setLogs(prev => [...prev, { status: 'stopped', message: 'Training stopped' }])
    } catch (err) {
      console.error('Failed to stop training:', err)
    }
  }

  const progressPercent = progress.total ? (progress.epoch / progress.total) * 100 : 0

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-2xl font-bold">Training</h2>
        <div className="flex items-center gap-4">
          {status && (
            <span className={`text-sm ${
              status.includes('Error') ? 'text-red-400' : 
              status.includes('started') ? 'text-green-400' : 
              status.includes('initializing') ? 'text-yellow-400' :
              'text-primary-400'
            }`}>{status}</span>
          )}
          <div className="flex gap-3">
            {!isTraining ? (
              <button onClick={handleStartTraining} className="btn-primary">
                Start Training
              </button>
            ) : (
              <button onClick={handleStopTraining} className="btn-secondary">
                Stop Training
              </button>
            )}
          </div>
        </div>
      </div>

      {isTraining && (
        <div className="card">
          <div className="flex items-center justify-between mb-4">
            <span className="text-lg font-semibold">Progress</span>
            <span className="text-primary-400">
              Epoch {progress.epoch} {progress.continuous || !progress.total ? '(∞)' : `/ ${progress.total}`}
            </span>
          </div>
          <div className="w-full bg-dark-800 rounded-full h-2 mb-4">
            <div
              className="bg-primary-500 h-2 rounded-full transition-all duration-300"
              style={{ width: `${progressPercent}%` }}
            ></div>
          </div>
          <div className="text-sm text-gray-400">
            Loss: {progress.loss.toFixed(6)}
          </div>
        </div>
      )}

      <div className="card">
        <h3 className="text-lg font-semibold mb-4">Training Log</h3>
        <div className="bg-dark-800 rounded-lg p-4 h-96 overflow-y-auto font-mono text-sm">
          {logs.length === 0 ? (
            <p className="text-gray-500">No training activity yet...</p>
          ) : (
            logs.map((log, index) => (
              <div key={index} className="mb-2">
                <span className="text-gray-500">[{new Date().toLocaleTimeString()}]</span>
                <span className={`ml-2 ${
                  log.status === 'error' ? 'text-red-400' :
                  log.status === 'started' ? 'text-green-400' :
                  log.status === 'stopped' ? 'text-yellow-400' :
                  'text-blue-400'
                }`}>
                  {log.message || JSON.stringify(log)}
                </span>
              </div>
            ))
          )}
          <div ref={logEndRef} />
        </div>
      </div>
    </div>
  )
}

export default TrainingPanel
