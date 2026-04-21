import React from 'react'

function StatusCard({ title, status, statusColor, metrics }) {
  return (
    <div className="card">
      <h3 className="text-gray-400 text-sm font-medium mb-2">{title}</h3>
      <div className={`text-2xl font-bold mb-4 ${statusColor}`}>{status}</div>
      <div className="space-y-2">
        {metrics.map((metric, index) => (
          <div key={index} className="flex justify-between text-sm">
            <span className="text-gray-500">{metric.label}</span>
            <span className="text-white font-medium">{metric.value}</span>
          </div>
        ))}
      </div>
    </div>
  )
}

export default StatusCard
