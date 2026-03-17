import axios, { AxiosInstance, AxiosRequestConfig, AxiosResponse, AxiosError } from 'axios';

// API Configuration
const API_BASE_URL = import.meta.env.VITE_API_BASE_URL || 'http://localhost:8080';
const WS_URL = import.meta.env.VITE_WS_URL || 'ws://localhost:8080/ws';

// Custom error types
export class ApiError extends Error {
  constructor(
    message: string,
    public status: number,
    public code?: string,
    public details?: any
  ) {
    super(message);
    this.name = 'ApiError';
  }
}

export class NetworkError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'NetworkError';
  }
}

// Response types
export interface ApiResponse<T = any> {
  data: T;
  message?: string;
  success: boolean;
  timestamp: string;
}

export interface ValidationError {
  field: string;
  message: string;
  code: string;
}

export interface ApiErrorResponse {
  error: {
    message: string;
    code: string;
    details?: any;
    validationErrors?: ValidationError[];
  };
  success: false;
  timestamp: string;
}

// Request configuration
interface ApiRequestConfig extends AxiosRequestConfig {
  retryCount?: number;
  showToast?: boolean;
}

// Create axios instance
const createApiClient = (): AxiosInstance => {
  const client = axios.create({
    baseURL: API_BASE_URL,
    timeout: 30000,
    headers: {
      'Content-Type': 'application/json',
      'Accept': 'application/json',
    },
  });

  // Request interceptor
  client.interceptors.request.use(
    (config) => {
      // Add auth token if available
      const token = localStorage.getItem('auth_token');
      if (token) {
        config.headers.Authorization = `Bearer ${token}`;
      }
      
      // Add request timestamp
      config.metadata = { startTime: new Date() };
      
      return config;
    },
    (error) => {
      return Promise.reject(error);
    }
  );

  // Response interceptor
  client.interceptors.response.use(
    (response: AxiosResponse) => {
      // Log response time
      const config = response.config as any;
      const duration = new Date().getTime() - config.metadata.startTime.getTime();
      
      if (duration > 5000) {
        console.warn(`Slow API response: ${response.config.url} took ${duration}ms`);
      }

      // Handle standard API response format
      const apiResponse = response.data;
      
      if (apiResponse.success === false) {
        throw new ApiError(
          apiResponse.error?.message || 'Request failed',
          response.status,
          apiResponse.error?.code,
          apiResponse.error?.details
        );
      }

      return response;
    },
    (error: AxiosError) => {
      // Handle network errors
      if (!error.response) {
        const networkError = new NetworkError('Network error - please check your connection');
        // Will be handled by global error boundary
        return Promise.reject(networkError);
      }

      const status = error.response.status;
      const errorData = error.response.data as ApiErrorResponse;
      
      let errorMessage = 'An unexpected error occurred';
      let errorCode = 'UNKNOWN_ERROR';

      if (errorData?.error) {
        errorMessage = errorData.error.message;
        errorCode = errorData.error.code;
      } else if (error.message) {
        errorMessage = error.message;
      }

      // Handle specific status codes
      switch (status) {
        case 400:
          errorMessage = errorData?.error?.validationErrors 
            ? 'Please check your input and try again'
            : errorMessage;
          break;
        case 401:
          errorMessage = 'Authentication required. Please log in.';
          // Clear auth token
          localStorage.removeItem('auth_token');
          // Could trigger auth redirect here
          break;
        case 403:
          errorMessage = 'You do not have permission to perform this action';
          break;
        case 404:
          errorMessage = 'The requested resource was not found';
          break;
        case 429:
          errorMessage = 'Too many requests. Please try again later';
          break;
        case 500:
          errorMessage = 'Server error. Please try again later';
          break;
        case 502:
        case 503:
        case 504:
          errorMessage = 'Service temporarily unavailable. Please try again later';
          break;
      }

      const apiError = new ApiError(errorMessage, status, errorCode, errorData?.error?.details);
      
      return Promise.reject(apiError);
    }
  );

  return client;
};

// Create API client instance
const apiClient = createApiClient();

// Retry mechanism for failed requests
const retryRequest = async <T>(
  requestFn: () => Promise<AxiosResponse<T>>,
  maxRetries: number = 3
): Promise<AxiosResponse<T>> => {
  try {
    return await requestFn();
  } catch (error) {
    if (error instanceof NetworkError || (error instanceof ApiError && error.status >= 500)) {
      if (maxRetries > 0) {
        // Exponential backoff
        const delay = (4 - maxRetries) * 1000;
        await new Promise(resolve => setTimeout(resolve, delay));
        return retryRequest(requestFn, maxRetries - 1);
      }
    }
    throw error;
  }
};

// API methods
export const api = {
  // Authentication
  auth: {
    login: async (credentials: { username: string; password: string }) => {
      const response = await apiClient.post<ApiResponse<{ token: string }>>('/api/auth/login', credentials);
      if (response.data.data.token) {
        localStorage.setItem('auth_token', response.data.data.token);
      }
      return response.data;
    },
    
    logout: async () => {
      await apiClient.post('/api/auth/logout');
      localStorage.removeItem('auth_token');
    },
    
    refreshToken: async () => {
      const response = await apiClient.post<ApiResponse<{ token: string }>>('/api/auth/refresh');
      if (response.data.data.token) {
        localStorage.setItem('auth_token', response.data.data.token);
      }
      return response.data;
    },
  },

  // Circuits
  circuits: {
    getAll: async () => {
      const response = await apiClient.get<ApiResponse<any[]>>('/api/circuits');
      return response.data;
    },
    
    getById: async (id: string) => {
      const response = await apiClient.get<ApiResponse<any>>(`/api/circuits/${id}`);
      return response.data;
    },
    
    create: async (circuit: any) => {
      const response = await apiClient.post<ApiResponse<any>>('/api/circuits', circuit);
      return response.data;
    },
    
    update: async (id: string, circuit: any) => {
      const response = await apiClient.put<ApiResponse<any>>(`/api/circuits/${id}`, circuit);
      return response.data;
    },
    
    delete: async (id: string) => {
      const response = await apiClient.delete<ApiResponse<void>>(`/api/circuits/${id}`);
      return response.data;
    },
  },

  // Hardware
  hardware: {
    getAll: async () => {
      const response = await apiClient.get<ApiResponse<any[]>>('/api/hardware');
      return response.data;
    },
    
    getById: async (id: string) => {
      const response = await apiClient.get<ApiResponse<any>>(`/api/hardware/${id}`);
      return response.data;
    },
    
    create: async (hardware: any) => {
      const response = await apiClient.post<ApiResponse<any>>('/api/hardware', hardware);
      return response.data;
    },
    
    update: async (id: string, hardware: any) => {
      const response = await apiClient.put<ApiResponse<any>>(`/api/hardware/${id}`, hardware);
      return response.data;
    },
  },

  // Quantum
  quantum: {
    getBackends: async () => {
      const response = await apiClient.get<ApiResponse<any[]>>('/api/quantum/backends');
      return response.data;
    },
    
    testConnection: async (backendId: string) => {
      const response = await apiClient.post<ApiResponse<{ connected: boolean }>>(
        `/api/quantum/${backendId}/test`
      );
      return response.data;
    },
  },

  // Deployment
  deploy: {
    getConfig: async () => {
      const response = await apiClient.get<ApiResponse<any>>('/api/deploy/config');
      return response.data;
    },
    
    updateConfig: async (config: any) => {
      const response = await apiClient.put<ApiResponse<any>>('/api/deploy/config', config);
      return response.data;
    },
    
    deploy: async (deployment: any) => {
      const response = await apiClient.post<ApiResponse<{ jobId: string }>>(
        '/api/deploy',
        deployment
      );
      return response.data;
    },
  },

  // Training
  training: {
    estimate: async (config: any) => {
      const response = await apiClient.post<ApiResponse<any>>(
        '/api/training/estimate',
        config
      );
      return response.data;
    },
    
    start: async (config: any) => {
      const response = await apiClient.post<ApiResponse<{ trainingId: string }>>(
        '/api/training/start',
        config
      );
      return response.data;
    },
  },

  // Job Configuration
  jobConfig: {
    getSummary: async () => {
      const response = await apiClient.get<ApiResponse<any>>('/api/job-config/summary');
      return response.data;
    },
    
    validate: async (config: any) => {
      const response = await apiClient.post<ApiResponse<{ valid: boolean; errors?: ValidationError[] }>>(
        '/api/job-config/validate',
        config
      );
      return response.data;
    },
  },

  // Health check
  health: {
    check: async () => {
      const response = await apiClient.get<ApiResponse<{ status: string }>>('/health');
      return response.data;
    },
  },

  // Generic request method with retry
  request: async <T>(config: ApiRequestConfig): Promise<ApiResponse<T>> => {
    const { retryCount = 3, showToast = true, ...axiosConfig } = config;
    
    try {
      const response = await retryRequest(
        () => apiClient.request<ApiResponse<T>>(axiosConfig),
        retryCount
      );
      
      return response.data;
    } catch (error) {
      // Error handling is done in the interceptor and global error boundary
      throw error;
    }
  },
};

// WebSocket service
export class WebSocketService {
  private socket: WebSocket | null = null;
  private url: string;
  private reconnectAttempts = 0;
  private maxReconnectAttempts = 5;
  private reconnectDelay = 1000;
  private listeners: Map<string, Function[]> = new Map();

  constructor(url: string = WS_URL) {
    this.url = url;
  }

  connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      try {
        this.socket = new WebSocket(this.url);
        
        this.socket.onopen = () => {
          this.reconnectAttempts = 0;
          this.emit('open');
          resolve();
        };
        
        this.socket.onmessage = (event) => {
          try {
            const data = JSON.parse(event.data);
            this.emit('message', data);
          } catch (error) {
            console.error('Failed to parse WebSocket message:', error);
          }
        };
        
        this.socket.onclose = () => {
          this.socket = null;
          this.emit('close');
          
          // Attempt to reconnect
          if (this.reconnectAttempts < this.maxReconnectAttempts) {
            setTimeout(() => {
              this.reconnectAttempts++;
              this.connect();
            }, this.reconnectDelay * Math.pow(2, this.reconnectAttempts));
          }
        };
        
        this.socket.onerror = (error) => {
          this.emit('error', error);
          reject(error);
        };
        
      } catch (error) {
        reject(error);
      }
    });
  }

  disconnect(): void {
    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }
  }

  send(data: any): void {
    if (this.socket && this.socket.readyState === WebSocket.OPEN) {
      this.socket.send(JSON.stringify(data));
    } else {
      console.warn('WebSocket not connected, cannot send message');
    }
  }

  on(event: string, callback: Function): void {
    if (!this.listeners.has(event)) {
      this.listeners.set(event, []);
    }
    this.listeners.get(event)!.push(callback);
  }

  off(event: string, callback: Function): void {
    if (this.listeners.has(event)) {
      const callbacks = this.listeners.get(event)!;
      const index = callbacks.indexOf(callback);
      if (index > -1) {
        callbacks.splice(index, 1);
      }
    }
  }

  private emit(event: string, ...args: any[]): void {
    if (this.listeners.has(event)) {
      this.listeners.get(event)!.forEach(callback => {
        try {
          callback(...args);
        } catch (error) {
          console.error(`Error in WebSocket event listener for '${event}':`, error);
        }
      });
    }
  }
}

// Create WebSocket instance
export const wsService = new WebSocketService();

// Export types
export type { ApiRequestConfig };
export { ApiError, NetworkError };