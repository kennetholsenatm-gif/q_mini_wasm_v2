import React, { useState, useEffect, useRef } from 'react'
import Dashboard from './components/Dashboard'
import TrainingPanel from './components/TrainingPanel'
import DataAcquisition from './components/DataAcquisition'
import InferencePanel from './components/InferencePanel'
import ConfigPanel from './components/ConfigPanel'
import Header from './components/Header'
import Sidebar from './components/Sidebar'
import { AppProvider } from './context/AppContext'

function App() {
  const [activeTab, setActiveTab] = useState('dashboard')
  const [serverStatus, setServerStatus] = useState('connecting')
  
  // Global training state - persists across tab switches
  const [isTraining, setIsTraining] = useState(false)
  const [trainingLogs, setTrainingLogs] = useState([])
  const [trainingProgress, setTrainingProgress] = useState({ epoch: 0, total: null, loss: 0, continuous: true })
  const [trainingStatus, setTrainingStatus] = useState('')
  const eventSourceRef = useRef(null)

  // Keep SSE connection alive globally
  useEffect(() => {
    if (isTraining && !eventSourceRef.current) {
      const es = new EventSource('/training-stream')
      eventSourceRef.current = es

      es.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data)
          setTrainingLogs(prev => [...prev, data])
          
          if (data.epoch !== undefined) {
            setTrainingProgress(prev => ({
              epoch: data.epoch,
              total: data.total_epochs || prev.total,
              loss: data.loss || 0,
              continuous: data.continuous !== undefined ? data.continuous : prev.continuous,
            }))
          }
          if (data.status === 'stopped' || data.status === 'finished') {
            setIsTraining(false)
          }
        } catch (err) {
          console.error('Failed to parse SSE message:', err)
        }
      }

      es.onerror = () => {
        console.error('SSE connection error')
        es.close()
        eventSourceRef.current = null
      }
    }
    
    return () => {
      // Don't close on unmount - only close when training stops
    }
  }, [isTraining])

  // Close SSE when training stops
  useEffect(() => {
    if (!isTraining && eventSourceRef.current) {
      eventSourceRef.current.close()
      eventSourceRef.current = null
    }
  }, [isTraining])

  useEffect(() => {
    fetch('/mcp', { method: 'POST', body: JSON.stringify({ method: 'wui_connect' }) })
      .then(() => setServerStatus('connected'))
      .catch(() => setServerStatus('disconnected'))
  }, [])

  const renderContent = () => {
    switch (activeTab) {
      case 'dashboard':
        return <Dashboard isTraining={isTraining} trainingProgress={trainingProgress} />
      case 'training':
        return <TrainingPanel 
          isTraining={isTraining} 
          setIsTraining={setIsTraining}
          logs={trainingLogs}
          setLogs={setTrainingLogs}
          progress={trainingProgress}
          setProgress={setTrainingProgress}
          status={trainingStatus}
          setStatus={setTrainingStatus}
        />
      case 'data':
        return <DataAcquisition isTraining={isTraining} />
      case 'inference':
        return <InferencePanel />
      case 'config':
        return <ConfigPanel />
      default:
        return <Dashboard isTraining={isTraining} trainingProgress={trainingProgress} />
    }
  }

  return (
    <AppProvider>
      <div className="min-h-screen bg-dark-950 text-white">
        <Header serverStatus={serverStatus} isTraining={isTraining} trainingProgress={trainingProgress} />
        <div className="flex">
          <Sidebar activeTab={activeTab} setActiveTab={setActiveTab} isTraining={isTraining} />
          <main className="flex-1 p-6 overflow-auto">
            {renderContent()}
          </main>
        </div>
      </div>
    </AppProvider>
  )
}

export default App
