import React, { createContext, useContext, useState, useCallback, ReactNode } from 'react';
import './Toast.css';

export interface Toast {
  id: string;
  type: 'success' | 'error' | 'warning' | 'info';
  message: string;
  duration?: number;
  persistent?: boolean;
}

interface ToastContextType {
  toasts: Toast[];
  addToast: (toast: Omit<Toast, 'id'>) => void;
  removeToast: (id: string) => void;
  success: (message: string, options?: { duration?: number; persistent?: boolean }) => void;
  error: (message: string, options?: { duration?: number; persistent?: boolean }) => void;
  warning: (message: string, options?: { duration?: number; persistent?: boolean }) => void;
  info: (message: string, options?: { duration?: number; persistent?: boolean }) => void;
}

const ToastContext = createContext<ToastContextType | undefined>(undefined);

export const useToast = () => {
  const context = useContext(ToastContext);
  if (!context) {
    throw new Error('useToast must be used within a ToastProvider');
  }
  return context;
};

interface ToastProviderProps {
  children: ReactNode;
}

export const ToastProvider: React.FC<ToastProviderProps> = ({ children }) => {
  const [toasts, setToasts] = useState<Toast[]>([]);

  const removeToast = useCallback((id: string) => {
    setToasts(prev => prev.filter(toast => toast.id !== id));
  }, []);

  const addToast = useCallback((toastData: Omit<Toast, 'id'>) => {
    const id = `toast-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
    const toast: Toast = { ...toastData, id };
    
    setToasts(prev => [...prev, toast]);

    // Auto-remove toast after duration
    if (!toast.persistent && toast.duration !== 0) {
      const duration = toast.duration || 5000;
      setTimeout(() => {
        removeToast(id);
      }, duration);
    }
  }, [removeToast]);

  const success = useCallback((message: string, options?: { duration?: number; persistent?: boolean }) => {
    addToast({ type: 'success', message, ...options });
  }, [addToast]);

  const error = useCallback((message: string, options?: { duration?: number; persistent?: boolean }) => {
    addToast({ type: 'error', message, ...options });
  }, [addToast]);

  const warning = useCallback((message: string, options?: { duration?: number; persistent?: boolean }) => {
    addToast({ type: 'warning', message, ...options });
  }, [addToast]);

  const info = useCallback((message: string, options?: { duration?: number; persistent?: boolean }) => {
    addToast({ type: 'info', message, ...options });
  }, [addToast]);

  const contextValue: ToastContextType = {
    toasts,
    addToast,
    removeToast,
    success,
    error,
    warning,
    info,
  };

  return (
    <ToastContext.Provider value={contextValue}>
      {children}
      <ToastContainer />
    </ToastContext.Provider>
  );
};

const ToastContainer: React.FC = () => {
  const { toasts, removeToast } = useToast();

  if (toasts.length === 0) {
    return null;
  }

  return (
    <div className="toast-container" role="region" aria-live="polite" aria-label="Notifications">
      {toasts.map((toast) => (
        <ToastItem key={toast.id} toast={toast} onRemove={removeToast} />
      ))}
    </div>
  );
};

interface ToastItemProps {
  toast: Toast;
  onRemove: (id: string) => void;
}

const ToastItem: React.FC<ToastItemProps> = ({ toast, onRemove }) => {
  const getIcon = () => {
    switch (toast.type) {
      case 'success':
        return (
          <svg className="toast-icon" viewBox="0 0 24 24" fill="none" aria-hidden="true">
            <path
              d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"
              fill="currentColor"
            />
          </svg>
        );
      case 'error':
        return (
          <svg className="toast-icon" viewBox="0 0 24 24" fill="none" aria-hidden="true">
            <path
              d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-2h2v2zm0-4h-2V7h2v6z"
              fill="currentColor"
            />
          </svg>
        );
      case 'warning':
        return (
          <svg className="toast-icon" viewBox="0 0 24 24" fill="none" aria-hidden="true">
            <path
              d="M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z"
              fill="currentColor"
            />
          </svg>
        );
      case 'info':
        return (
          <svg className="toast-icon" viewBox="0 0 24 24" fill="none" aria-hidden="true">
            <path
              d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-6h2v6zm0-8h-2V7h2v2z"
              fill="currentColor"
            />
          </svg>
        );
      default:
        return null;
    }
  };

  return (
    <div
      className={`toast toast-${toast.type}`}
      role="alert"
      aria-describedby={`toast-message-${toast.id}`}
      onAnimationEnd={(e) => {
        if (e.animationName === 'toastSlideOut') {
          onRemove(toast.id);
        }
      }}
    >
      <div className="toast-content">
        {getIcon()}
        <div className="toast-text" id={`toast-message-${toast.id}`}>
          {toast.message}
        </div>
      </div>
      <button
        className="toast-close"
        onClick={() => onRemove(toast.id)}
        aria-label="Close notification"
        tabIndex={0}
      >
        <svg className="toast-close-icon" viewBox="0 0 24 24" fill="none" aria-hidden="true">
          <path
            d="M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12 19 6.41z"
            fill="currentColor"
          />
        </svg>
      </button>
    </div>
  );
};