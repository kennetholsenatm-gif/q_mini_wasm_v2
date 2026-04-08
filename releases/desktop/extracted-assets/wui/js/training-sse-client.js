/**
 * Training SSE Client - Live streaming of training progress
 * 
 * Connects to SSE endpoint and displays real-time training metrics
 */

class TrainingSseClient {
    constructor(eventSourceUrl = 'http://localhost:9090/training-stream') {
        this.eventSourceUrl = eventSourceUrl;
        this.eventSource = null;
        this.isConnected = false;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
        this.reconnectDelay = 2000;
        this.onMessageCallback = null;
        this.onErrorCallback = null;
        this.onConnectCallback = null;
        this.buffer = [];
        this.maxBufferSize = 100;
    }

    /**
     * Connect to SSE stream
     */
    connect() {
        if (this.isConnected) {
            console.log('[SSE] Already connected');
            return;
        }

        console.log('[SSE] Connecting to', this.eventSourceUrl);
        
        try {
            this.eventSource = new EventSource(this.eventSourceUrl);
            
            this.eventSource.onopen = () => {
                console.log('[SSE] Connected');
                this.isConnected = true;
                this.reconnectAttempts = 0;
                if (this.onConnectCallback) {
                    this.onConnectCallback();
                }
            };
            
            this.eventSource.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this.buffer.push({
                        timestamp: new Date().toISOString(),
                        data: data
                    });
                    
                    // Keep buffer size limited
                    if (this.buffer.length > this.maxBufferSize) {
                        this.buffer.shift();
                    }
                    
                    if (this.onMessageCallback) {
                        this.onMessageCallback(data);
                    }
                } catch (e) {
                    console.error('[SSE] Parse error:', e);
                }
            };
            
            this.eventSource.onerror = (error) => {
                console.error('[SSE] Error:', error);
                this.isConnected = false;
                
                if (this.onErrorCallback) {
                    this.onErrorCallback(error);
                }
                
                // Auto-reconnect
                if (this.reconnectAttempts < this.maxReconnectAttempts) {
                    this.reconnectAttempts++;
                    console.log(`[SSE] Reconnecting... (${this.reconnectAttempts}/${this.maxReconnectAttempts})`);
                    setTimeout(() => this.connect(), this.reconnectDelay);
                }
            };
            
        } catch (e) {
            console.error('[SSE] Connection failed:', e);
            this.isConnected = false;
            if (this.onErrorCallback) {
                this.onErrorCallback(e);
            }
        }
    }

    /**
     * Disconnect from SSE stream
     */
    disconnect() {
        if (this.eventSource) {
            this.eventSource.close();
            this.eventSource = null;
        }
        this.isConnected = false;
        console.log('[SSE] Disconnected');
    }

    /**
     * Set message handler
     */
    onMessage(callback) {
        this.onMessageCallback = callback;
    }

    /**
     * Set error handler
     */
    onError(callback) {
        this.onErrorCallback = callback;
    }

    /**
     * Set connect handler
     */
    onConnect(callback) {
        this.onConnectCallback = callback;
    }

    /**
     * Get current connection status
     */
    getStatus() {
        return {
            connected: this.isConnected,
            reconnectAttempts: this.reconnectAttempts,
            bufferSize: this.buffer.length
        };
    }

    /**
     * Get buffered messages
     */
    getBuffer() {
        return [...this.buffer];
    }

    /**
     * Clear message buffer
     */
    clearBuffer() {
        this.buffer = [];
    }
}

// Export for use
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { TrainingSseClient };
} else if (typeof window !== 'undefined') {
    window.TrainingSseClient = TrainingSseClient;
}
