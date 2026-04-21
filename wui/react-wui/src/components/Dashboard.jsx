import React, { useEffect, useState } from 'react'
import StatusCard from './StatusCard'

function Dashboard({ isTraining, trainingProgress }) {
  const [stats, setStats] = useState({
    training: { running: isTraining, epoch: trainingProgress?.epoch || 0, samples: 0 },
    data: { sources: 0, size: '0 MB' },
    model: { experts: 0, active: 0, topology: null, generation: null },
  })
  
  // Sync with global training state
  useEffect(() => {
    setStats(prev => ({
      ...prev,
      training: {
        ...prev.training,
        running: isTraining,
        epoch: trainingProgress?.epoch || prev.training.epoch,
      }
    }))
  }, [isTraining, trainingProgress])

  useEffect(() => {
    // Fetch initial stats
    fetchStats()
    const interval = setInterval(fetchStats, 5000)
    return () => clearInterval(interval)
  }, [])

  const fetchStats = async () => {
    try {
      const response = await fetch('/mcp', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 1,
          method: 'wui_get_ops_snapshot',
          params: {},
        }),
      })
      const data = await response.json()
      if (data.result) {
        setStats({
          training: data.result.training || stats.training,
          data: data.result.data || stats.data,
          model: data.result.model || stats.model,
        })
      }
    } catch (err) {
      console.error('Failed to fetch stats:', err)
    }
  }

  return (
    <div className="space-y-6">
      <h2 className="text-2xl font-bold">Dashboard</h2>
      
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
        <StatusCard
          title="Training Status"
          status={stats.training.running ? 'Running' : 'Idle'}
          statusColor={stats.training.running ? 'text-green-400' : 'text-gray-400'}
          metrics={[
            { label: 'Epoch', value: stats.training.epoch || 0 },
            { label: 'Samples', value: (stats.training.samples || 0).toLocaleString() },
          ]}
        />
        
        <StatusCard
          title="Data Sources"
          status={`${stats.data.sources} sources`}
          statusColor="text-blue-400"
          metrics={[
            { label: 'Storage', value: stats.data.size },
            { label: 'Last Update', value: 'Just now' },
          ]}
        />
        
        <StatusCard
          title="Model"
          status={`${stats.model.active} / ${stats.model.experts} experts`}
          statusColor="text-purple-400"
          metrics={[
            { label: 'Topology', value: stats.model.topology || 'N/A' },
            { label: 'Generation', value: stats.model.generation || 'N/A' },
          ]}
        />
      </div>

      <div className="card">
        <h3 className="text-lg font-semibold mb-4">System Status</h3>
        <div className="space-y-2">
          <div className="flex justify-between">
            <span className="text-gray-400">Backend</span>
            <span className="text-green-400">Connected</span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Training Pipeline</span>
            <span className={stats.training.running ? 'text-green-400' : 'text-gray-400'}>
              {stats.training.running ? 'Active' : 'Standby'}
            </span>
          </div>
          <div className="flex justify-between">
            <span className="text-gray-400">Data Acquisition</span>
            <span className="text-gray-400">Idle</span>
          </div>
        </div>
      </div>
    </div>
  )
}

export default Dashboard
