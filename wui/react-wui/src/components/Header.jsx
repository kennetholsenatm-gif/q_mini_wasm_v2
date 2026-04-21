import React from 'react'

function Header({ serverStatus }) {
  const statusColors = {
    connected: 'bg-green-500',
    disconnected: 'bg-red-500',
    connecting: 'bg-yellow-500',
  }

  return (
    <header className="bg-dark-900 border-b border-dark-800 px-6 py-4">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <div className="w-10 h-10 bg-primary-600 rounded-lg flex items-center justify-center font-bold text-xl">
            Q
          </div>
          <div>
            <h1 className="text-xl font-bold">Q-Mini-WASM-v2</h1>
            <p className="text-sm text-gray-400">Quantum-Enhanced AI Training</p>
          </div>
        </div>
        <div className="flex items-center gap-4">
          <div className="flex items-center gap-2">
            <div className={`w-2 h-2 rounded-full ${statusColors[serverStatus]}`}></div>
            <span className="text-sm text-gray-400 capitalize">{serverStatus}</span>
          </div>
        </div>
      </div>
    </header>
  )
}

export default Header
