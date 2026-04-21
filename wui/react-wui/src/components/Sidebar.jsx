import React from 'react'

const menuItems = [
  { id: 'dashboard', label: 'Dashboard', icon: '📊' },
  { id: 'training', label: 'Training', icon: '🎯' },
  { id: 'data', label: 'Data Acquisition', icon: '📥' },
  { id: 'inference', label: 'Inference', icon: '🧠' },
  { id: 'config', label: 'Configuration', icon: '⚙️' },
]

function Sidebar({ activeTab, setActiveTab, isTraining }) {
  return (
    <aside className="w-64 bg-dark-900 border-r border-dark-800 min-h-[calc(100vh-73px)]">
      <nav className="p-4">
        <ul className="space-y-2">
          {menuItems.map((item) => (
            <li key={item.id}>
              <button
                onClick={() => setActiveTab(item.id)}
                className={`w-full flex items-center gap-3 px-4 py-3 rounded-lg text-left transition-colors ${
                  activeTab === item.id
                    ? 'bg-primary-600 text-white'
                    : 'text-gray-400 hover:bg-dark-800 hover:text-white'
                }`}
              >
                <span>{item.icon}</span>
                <span className="font-medium">{item.label}</span>
                {item.id === 'training' && isTraining && (
                  <span className="ml-auto flex h-2 w-2">
                    <span className="animate-ping absolute inline-flex h-2 w-2 rounded-full bg-green-400 opacity-75"></span>
                    <span className="relative inline-flex rounded-full h-2 w-2 bg-green-500"></span>
                  </span>
                )}
              </button>
            </li>
          ))}
        </ul>
      </nav>
    </aside>
  )
}

export default Sidebar
