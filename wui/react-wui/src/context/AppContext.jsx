import React, { createContext, useContext, useState } from 'react'

const AppContext = createContext()

export function AppProvider({ children }) {
  const [serverStatus, setServerStatus] = useState('disconnected')
  const [trainingStatus, setTrainingStatus] = useState({
    running: false,
    epoch: 0,
    samples: 0,
    loss: 0,
  })
  const [dataStatus, setDataStatus] = useState({
    sources: 0,
    size: '0 MB',
  })

  const value = {
    serverStatus,
    setServerStatus,
    trainingStatus,
    setTrainingStatus,
    dataStatus,
    setDataStatus,
  }

  return <AppContext.Provider value={value}>{children}</AppContext.Provider>
}

export function useApp() {
  return useContext(AppContext)
}
