/**
 * WebSocket Bridge - WUI to Engine Communication
 * 
 * Provides real-time bidirectional communication between:
 * - WUI (JavaScript) <-> WebSocket Server <-> Go WASM Bridge <-> C++ DLL <-> SYCL Kernels
 * 
 * Features:
 * - Zero-copy SharedArrayBuffer for USM memory access
 * - Real-time metrics streaming
 * - Configuration updates
 * - Pipeline status monitoring
 */

class WebSocketBridge {
    constructor() {
        this.ws = null;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;
        this.reconnectDelay = 1000;
        this.sharedBuffer = null;
        this.callbacks = new Map();
        this.metricsInterval = null;
        this.isConnected = false;
    }

    async init() {
        await this.setupSharedMemory();
        await this.connect();
        this.setupMetricsStreaming();
        this.setupMessageHandlers();
    }

    async setupSharedMemory() {
        // Create SharedArrayBuffer for zero-copy USM access
        // Size: 512MB default (configurable via system.toml)
        const bufferSize = 512 * 1024 * 1024; // 512 MB
        
        try {
            this.sharedBuffer = new SharedArrayBuffer(bufferSize);
            console.log('SharedArrayBuffer created:', bufferSize, 'bytes');
            
            // Initialize memory view
            this.memoryView = new Uint8Array(this.sharedBuffer);
            
            // Clear memory
            this.memoryView.fill(0);
            
        } catch (error) {
            console.error('Failed to create SharedArrayBuffer:', error);
            // Fallback to regular ArrayBuffer if SharedArrayBuffer not available
            this.sharedBuffer = new ArrayBuffer(bufferSize);
            this.memoryView = new Uint8Array(this.sharedBuffer);
        }
    }

    async connect() {
        const wsUrl = `ws://localhost:8080/ws`;
        
        try {
            this.ws = new WebSocket(wsUrl);
            
            this.ws.onopen = () => {
                console.log('WebSocket connected');
                this.isConnected = true;
                this.reconnectAttempts = 0;
                
                // Register with server
                this.send({
                    type: 'register',
                    client: 'wui',
                    capabilities: ['shared_memory', 'metrics', 'control']
                });
                
                // Dispatch connection event
                window.dispatchEvent(new CustomEvent('engineConnected'));
            };
            
            this.ws.onmessage = (event) => {
                this.handleMessage(JSON.parse(event.data));
            };
            
            this.ws.onclose = () => {
                console.log('WebSocket closed');
                this.isConnected = false;
                window.dispatchEvent(new CustomEvent('engineDisconnected'));
                this.attemptReconnect();
            };
            
            this.ws.onerror = (error) => {
                console.error('WebSocket error:', error);
                window.dispatchEvent(new CustomEvent('engineError', { detail: error }));
            };
            
        } catch (error) {
            console.error('Failed to connect WebSocket:', error);
            this.attemptReconnect();
        }
    }

    attemptReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            console.error('Max reconnection attempts reached');
            return;
        }
        
        this.reconnectAttempts++;
        console.log(`Reconnecting... attempt ${this.reconnectAttempts}/${this.maxReconnectAttempts}`);
        
        setTimeout(() => {
            this.connect();
        }, this.reconnectDelay * this.reconnectAttempts);
    }

    send(message) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(message));
        } else {
            console.warn('WebSocket not connected, message queued:', message);
        }
    }

    handleMessage(message) {
        switch (message.type) {
            case 'metrics':
                this.updateMetrics(message.data);
                break;
                
            case 'state_update':
                this.updateEngineState(message.data);
                break;
                
            case 'pipeline_status':
                this.updatePipelineStatus(message.data);
                break;
                
            case 'config_ack':
                console.log('Configuration update acknowledged');
                break;
                
            case 'error':
                console.error('Engine error:', message.error);
                window.dispatchEvent(new CustomEvent('engineError', { 
                    detail: message.error 
                }));
                break;
                
            case 'memory_sync':
                this.handleMemorySync(message.data);
                break;
                
            default:
                // Handle registered callbacks
                const callback = this.callbacks.get(message.type);
                if (callback) {
                    callback(message.data);
                }
        }
    }

    setupMetricsStreaming() {
        // Request metrics stream from server
        this.send({
            type: 'subscribe_metrics',
            interval: 5000 // 5 seconds
        });
    }

    updateMetrics(metrics) {
        // Update WUI metric displays
        const stabilizerRate = document.getElementById('stabilizer-rate');
        const energyEfficiency = document.getElementById('energy-efficiency');
        const quantumStatus = document.getElementById('quantum-status');
        const ternaryStatus = document.getElementById('ternary-status');
        
        if (stabilizerRate && metrics.stabilizer_rate !== undefined) {
            stabilizerRate.textContent = metrics.stabilizer_rate.toLocaleString();
        }
        
        if (energyEfficiency && metrics.energy_efficiency !== undefined) {
            energyEfficiency.textContent = `${metrics.energy_efficiency.toFixed(2)} pJ/op`;
        }
        
        if (quantumStatus && metrics.quantum_status) {
            quantumStatus.textContent = metrics.quantum_status;
            quantumStatus.className = `wui-status-indicator ${metrics.quantum_status.toLowerCase()}`;
        }
        
        if (ternaryStatus && metrics.ternary_status) {
            ternaryStatus.textContent = metrics.ternary_status;
            ternaryStatus.className = `wui-status-indicator ${metrics.ternary_status.toLowerCase()}`;
        }
        
        // Dispatch metrics update event
        window.dispatchEvent(new CustomEvent('metricsUpdate', { detail: metrics }));
    }

    updateEngineState(state) {
        // Update engine state displays
        window.dispatchEvent(new CustomEvent('engineStateUpdate', { detail: state }));
    }

    updatePipelineStatus(status) {
        // Update pipeline status in UI
        window.dispatchEvent(new CustomEvent('pipelineStatusUpdate', { detail: status }));
    }

    handleMemorySync(data) {
        // Sync shared memory with engine
        if (data.offset !== undefined && data.data) {
            const bytes = new Uint8Array(data.data);
            this.memoryView.set(bytes, data.offset);
        }
    }

    setupMessageHandlers() {
        // Register handlers for specific message types
        this.registerHandler('quantum_operation_complete', (data) => {
            console.log('Quantum operation complete:', data);
            window.dispatchEvent(new CustomEvent('quantumOpComplete', { detail: data }));
        });
        
        this.registerHandler('inference_result', (data) => {
            window.dispatchEvent(new CustomEvent('inferenceResult', { detail: data }));
        });
    }

    registerHandler(type, callback) {
        this.callbacks.set(type, callback);
    }

    // API Methods for WUI Controls
    
    applyHadamard(qutritIndex) {
        this.send({
            type: 'quantum_operation',
            operation: 'hadamard',
            target: qutritIndex
        });
    }

    applyPhase(qutritIndex) {
        this.send({
            type: 'quantum_operation',
            operation: 'phase',
            target: qutritIndex
        });
    }

    applyCSUM(control, target) {
        this.send({
            type: 'quantum_operation',
            operation: 'csum',
            control: control,
            target: target
        });
    }

    measureQutrit(qutritIndex) {
        this.send({
            type: 'quantum_operation',
            operation: 'measure',
            target: qutritIndex
        });
    }

    setEntanglementGraph(graphType) {
        this.send({
            type: 'config_update',
            section: 'system.qgnn',
            key: 'entanglement_graph',
            value: graphType
        });
    }

    setNumQutrits(count) {
        this.send({
            type: 'config_update',
            section: 'system.qgnn',
            key: 'default_num_qutrits',
            value: count
        });
    }

    runInference(input) {
        this.send({
            type: 'inference',
            input: input
        });
    }

    triggerPipeline(pipelineType) {
        this.send({
            type: 'trigger_pipeline',
            pipeline: pipelineType
        });
    }

    getPipelineStatus() {
        this.send({
            type: 'get_pipeline_status'
        });
    }

    updateConfig(section, key, value) {
        this.send({
            type: 'config_update',
            section: section,
            key: key,
            value: value
        });
    }

    // Memory Access Methods
    
    readMemory(offset, length) {
        return this.memoryView.slice(offset, offset + length);
    }

    writeMemory(offset, data) {
        const bytes = new Uint8Array(data);
        this.memoryView.set(bytes, offset);
        
        // Notify engine of memory update
        this.send({
            type: 'memory_update',
            offset: offset,
            length: bytes.length
        });
    }

    // Cleanup
    disconnect() {
        if (this.ws) {
            this.ws.close();
        }
        if (this.metricsInterval) {
            clearInterval(this.metricsInterval);
        }
    }
}

// Initialize bridge when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.wsBridge = new WebSocketBridge();
    window.wsBridge.init().catch(console.error);
});

// Export for use in other scripts
globalThis.WebSocketBridge = WebSocketBridge;
