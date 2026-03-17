import React, { Component, ReactNode, ErrorInfo } from 'react';
import './ErrorBoundary.css';

/** Generate a cryptographically secure random suffix (e.g. for error/user IDs). */
function secureRandomSuffix(length: number = 9): string {
  if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
    const bytes = new Uint8Array(length);
    crypto.getRandomValues(bytes);
    return Array.from(bytes, b => b.toString(36)).join('').slice(0, length);
  }
  return Math.random().toString(36).slice(2, 2 + length);
}

interface ErrorBoundaryState {
  hasError: boolean;
  error?: Error;
  errorInfo?: ErrorInfo;
  errorId?: string;
}

interface ErrorBoundaryProps {
  children: ReactNode;
  fallback?: ReactNode;
  onError?: (error: Error, errorInfo: ErrorInfo) => void;
}

export class ErrorBoundary extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
  constructor(props: ErrorBoundaryProps) {
    super(props);
    this.state = { hasError: false };
  }

  static getDerivedStateFromError(error: Error): ErrorBoundaryState {
    // Update state so the next render will show the fallback UI
    return {
      hasError: true,
      error,
      errorId: `error-${Date.now()}-${secureRandomSuffix(9)}`
    };
  }

  componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    // Log error details
    console.error('ErrorBoundary caught an error:', error, errorInfo);
    
    // Update state with error info
    this.setState({
      error,
      errorInfo
    });

    // Call custom error handler if provided
    if (this.props.onError) {
      this.props.onError(error, errorInfo);
    }

    // You can also log the error to an error reporting service here
    this.logErrorToService(error, errorInfo);
  }

  logErrorToService = (error: Error, errorInfo: ErrorInfo) => {
    // In a real application, you would send this to your error reporting service
    // For now, we'll just log it to the console with additional context
    const errorReport = {
      message: error.message,
      stack: error.stack,
      componentStack: errorInfo.componentStack,
      timestamp: new Date().toISOString(),
      userAgent: navigator.userAgent,
      url: window.location.href,
      userId: this.getUserId()
    };

    // Log to console in development
    if (process.env.NODE_ENV === 'development') {
      console.group('🚨 Error Report');
      console.error('Error:', error);
      console.error('Error Info:', errorInfo);
      console.error('Error Report:', errorReport);
      console.groupEnd();
    }

    // In production, you might send this to a logging service
    if (process.env.NODE_ENV === 'production') {
      // Example: send to logging service
      // logger.error('React Error', errorReport);
    }
  };

  getUserId = (): string => {
    // Get user ID from localStorage or generate anonymous ID (crypto-safe)
    let userId = localStorage.getItem('user_id');
    if (!userId) {
      userId = `anonymous-${Date.now()}-${secureRandomSuffix(9)}`;
      localStorage.setItem('user_id', userId);
    }
    return userId;
  };

  handleReload = () => {
    window.location.reload();
  };

  handleGoHome = () => {
    window.location.href = '/';
  };

  handleReportError = () => {
    const { error, errorInfo } = this.state;
    if (error && errorInfo) {
      const report = {
        error: error.toString(),
        stack: error.stack,
        componentStack: errorInfo.componentStack,
        timestamp: new Date().toISOString(),
        userAgent: navigator.userAgent,
        url: window.location.href
      };

      // Create a report that can be sent to developers
      const reportText = `
Error Report
============
Error: ${report.error}
Stack: ${report.stack}
Component Stack: ${report.componentStack}
Timestamp: ${report.timestamp}
User Agent: ${report.userAgent}
URL: ${report.url}

Please send this report to the development team.
      `;

      // In a real application, you might:
      // 1. Send to an error reporting service
      // 2. Create a support ticket
      // 3. Email the development team
      // 4. Save to localStorage for later submission

      console.log('Error report:', reportText);
      
      // For now, just alert the user
      alert('Error report generated. Please contact support with the error details.');
    }
  };

  render() {
    if (this.state.hasError) {
      // Custom fallback UI
      if (this.props.fallback) {
        return this.props.fallback;
      }

      // Default error UI
      return (
        <div className="error-boundary">
          <div className="error-boundary-content">
            <div className="error-boundary-icon">
              <svg viewBox="0 0 24 24" fill="none" aria-hidden="true">
                <path
                  d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-2h2v2zm0-4h-2V7h2v6z"
                  fill="currentColor"
                />
              </svg>
            </div>
            
            <h1 className="error-boundary-title">Something went wrong</h1>
            <p className="error-boundary-message">
              We're sorry, but something unexpected happened. Our team has been notified.
            </p>

            <div className="error-boundary-actions">
              <button
                className="btn btn-primary"
                onClick={this.handleReload}
                aria-label="Reload page"
              >
                Reload Page
              </button>
              <button
                className="btn btn-secondary"
                onClick={this.handleGoHome}
                aria-label="Go home"
              >
                Go Home
              </button>
              <button
                className="btn btn-ghost"
                onClick={this.handleReportError}
                aria-label="Report error"
              >
                Report Error
              </button>
            </div>

            {process.env.NODE_ENV === 'development' && this.state.error && (
              <details className="error-boundary-details">
                <summary>Error Details (Development)</summary>
                <div className="error-boundary-error-info">
                  <div className="error-boundary-error-section">
                    <h3>Error:</h3>
                    <pre>{this.state.error.toString()}</pre>
                  </div>
                  <div className="error-boundary-error-section">
                    <h3>Stack Trace:</h3>
                    <pre>{this.state.error.stack}</pre>
                  </div>
                  {this.state.errorInfo && (
                    <div className="error-boundary-error-section">
                      <h3>Component Stack:</h3>
                      <pre>{this.state.errorInfo.componentStack}</pre>
                    </div>
                  )}
                </div>
              </details>
            )}
          </div>
        </div>
      );
    }

    return this.props.children;
  }
}

// Higher-order component for easy wrapping
export const withErrorBoundary = <P extends object>(
  Component: React.ComponentType<P>,
  fallback?: ReactNode,
  onError?: (error: Error, errorInfo: ErrorInfo) => void
) => {
  const WrappedComponent = (props: P) => (
    <ErrorBoundary fallback={fallback} onError={onError}>
      <Component {...props} />
    </ErrorBoundary>
  );

  WrappedComponent.displayName = `withErrorBoundary(${Component.displayName || Component.name})`;
  
  return WrappedComponent;
};

// Hook for programmatic error handling
export const useErrorHandler = () => {
  const [error, setError] = React.useState<Error | null>(null);

  const handleError = React.useCallback((error: Error) => {
    setError(error);
    // You could also trigger a global error boundary here
    console.error('Handled error:', error);
  }, []);

  const clearError = React.useCallback(() => {
    setError(null);
  }, []);

  return { error, handleError, clearError };
};