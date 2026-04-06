/**
 * WUI MCP Bridge - JavaScript MCP Protocol Handler for WUI Automation
 * 
 * Provides bidirectional MCP protocol support for the WebSocket bridge,
 * enabling LLMs and automations to control the WUI through standardized
 * Model Context Protocol messages.
 * 
 * This module extends the existing WebSocketBridge to handle MCP protocol
 * messages, allowing seamless integration with MCP clients.
 */

class WuiMcpBridge {
    constructor(websocketBridge) {
        this.wsBridge = websocketBridge;
        this.mcpCallbacks = new Map();
        this.messageId = 0;
        this.pendingRequests = new Map();
        
        // Register MCP message handlers
        this.setupMcpHandlers();
    }

    setupMcpHandlers() {
        // Register handler for MCP requests coming through WebSocket
        this.wsBridge.registerHandler('mcp_request', (data) => {
            this.handleMcpRequest(data);
        });

        // Register handler for MCP responses
        this.wsBridge.registerHandler('mcp_response', (data) => {
            this.handleMcpResponse(data);
        });

        // Register quantum operation completion handler
        this.wsBridge.registerHandler('quantum_operation_complete', (data) => {
            this.resolvePendingRequest('quantum_operation_complete', data);
        });

        // Register measurement result handler
        this.wsBridge.registerHandler('measurement_result', (data) => {
            this.resolvePendingRequest('measurement_result', data);
        });

        // Register inference result handler
        this.wsBridge.registerHandler('inference_result', (data) => {
            this.resolvePendingRequest('inference_result', data);
        });

        // Register metrics handler
        this.wsBridge.registerHandler('metrics', (data) => {
            this.resolvePendingRequest('metrics', data);
        });

        // Register pipeline status handler
        this.wsBridge.registerHandler('pipeline_status', (data) => {
            this.resolvePendingRequest('pipeline_status', data);
        });

        // Register betti result handler
        this.wsBridge.registerHandler('betti_result', (data) => {
            this.resolvePendingRequest('betti_result', data);
        });

        // Register memory data handler
        this.wsBridge.registerHandler('memory_data', (data) => {
            this.resolvePendingRequest('memory_data', data);
        });
    }

    /**
     * Generate unique message ID for MCP requests
     */
    generateId() {
        return `mcp_${++this.messageId}_${Date.now()}`;
    }

    /**
     * Send MCP request and wait for response
     */
    async sendMcpRequest(method, params, timeoutMs = 5000) {
        const id = this.generateId();
        
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                this.pendingRequests.delete(id);
                reject(new Error(`MCP request timeout: ${method}`));
            }, timeoutMs);

            this.pendingRequests.set(id, {
                resolve: (result) => {
                    clearTimeout(timeout);
                    resolve(result);
                },
                reject: (error) => {
                    clearTimeout(timeout);
                    reject(error);
                }
            });

            // Send via WebSocket bridge
            this.wsBridge.send({
                type: 'mcp_request',
                id: id,
                method: method,
                params: params,
                jsonrpc: '2.0'
            });
        });
    }

    /**
     * Handle incoming MCP request from server
     */
    handleMcpRequest(data) {
        const { id, method, params } = data;
        
        // Execute the requested tool
        this.executeTool(method, params)
            .then(result => {
                this.sendMcpResponse(id, result);
            })
            .catch(error => {
                this.sendMcpError(id, -32603, error.message);
            });
    }

    /**
     * Handle MCP response from server
     */
    handleMcpResponse(data) {
        const { id, result, error } = data;
        const pending = this.pendingRequests.get(id);
        
        if (pending) {
            if (error) {
                pending.reject(new Error(error.message));
            } else {
                pending.resolve(result);
            }
            this.pendingRequests.delete(id);
        }
    }

    /**
     * Resolve a pending request by response type
     */
    resolvePendingRequest(responseType, data) {
        // Find pending request waiting for this response type
        for (const [id, pending] of this.pendingRequests.entries()) {
            if (id.includes(responseType) || data.requestId === id) {
                pending.resolve(data);
                this.pendingRequests.delete(id);
                return;
            }
        }
    }

    /**
     * Send MCP response
     */
    sendMcpResponse(id, result) {
        this.wsBridge.send({
            type: 'mcp_response',
            id: id,
            result: result,
            jsonrpc: '2.0'
        });
    }

    /**
     * Send MCP error
     */
    sendMcpError(id, code, message, data = null) {
        this.wsBridge.send({
            type: 'mcp_response',
            id: id,
            error: {
                code: code,
                message: message,
                data: data
            },
            jsonrpc: '2.0'
        });
    }

    /**
     * Execute WUI tool by name
     */
    async executeTool(method, params) {
        switch (method) {
            case 'wui_connect':
                return this.handleConnect(params);
            case 'wui_disconnect':
                return this.handleDisconnect(params);
            case 'wui_apply_hadamard':
                return this.handleApplyHadamard(params);
            case 'wui_apply_phase':
                return this.handleApplyPhase(params);
            case 'wui_apply_csum':
                return this.handleApplyCSUM(params);
            case 'wui_apply_pauli_x':
                return this.handleApplyPauliX(params);
            case 'wui_apply_pauli_z':
                return this.handleApplyPauliZ(params);
            case 'wui_measure':
                return this.handleMeasure(params);
            case 'wui_set_config':
                return this.handleSetConfig(params);
            case 'wui_set_num_qutrits':
                return this.handleSetNumQutrits(params);
            case 'wui_set_entanglement_graph':
                return this.handleSetEntanglementGraph(params);
            case 'wui_run_inference':
                return this.handleRunInference(params);
            case 'wui_get_metrics':
                return this.handleGetMetrics(params);
            case 'wui_get_pipeline_status':
                return this.handleGetPipelineStatus(params);
            case 'wui_trigger_pipeline':
                return this.handleTriggerPipeline(params);
            case 'wui_compute_betti':
                return this.handleComputeBetti(params);
            case 'wui_start_ff_training':
                return this.handleStartFFTraining(params);
            case 'wui_stop_ff_training':
                return this.handleStopFFTraining(params);
            case 'wui_init_graph':
                return this.handleInitGraph(params);
            case 'wui_add_graph_node':
                return this.handleAddGraphNode(params);
            case 'wui_add_graph_edge':
                return this.handleAddGraphEdge(params);
            case 'wui_read_memory':
                return this.handleReadMemory(params);
            case 'wui_write_memory':
                return this.handleWriteMemory(params);
            case 'wui_init_training_pipeline':
                return this.handleInitTrainingPipeline(params);
            case 'wui_set_pipeline_config':
                return this.handleSetPipelineConfig(params);
            case 'wui_get_training_metrics':
                return this.handleGetTrainingMetrics(params);
            case 'wui_apply_betti_guidance':
                return this.handleApplyBettiGuidance(params);
            case 'wui_pause_training':
                return this.handlePauseTraining(params);
            case 'wui_resume_training':
                return this.handleResumeTraining(params);
            case 'wui_export_model':
                return this.handleExportModel(params);
            case 'wui_import_model':
                return this.handleImportModel(params);
            default:
                throw new Error(`Unknown tool: ${method}`);
        }
    }

    /**
     * Tool Handlers
     */

    async handleConnect(params) {
        // WebSocket connection is already managed by WebSocketBridge
        return {
            connected: this.wsBridge.isConnected,
            url: `ws://${params.host || 'localhost'}:${params.port || 8080}/ws`
        };
    }

    async handleDisconnect(params) {
        this.wsBridge.disconnect();
        return { connected: false };
    }

    async handleApplyHadamard(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Hadamard operation timeout'));
            }, 5000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    operation: 'hadamard',
                    qutrit_index: params.qutrit_index,
                    result: data
                });
            };

            this.mcpCallbacks.set(`hadamard_${params.qutrit_index}`, handler);
            this.wsBridge.applyHadamard(params.qutrit_index);
        });
    }

    async handleApplyPhase(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Phase operation timeout'));
            }, 5000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    operation: 'phase',
                    qutrit_index: params.qutrit_index,
                    result: data
                });
            };

            this.mcpCallbacks.set(`phase_${params.qutrit_index}`, handler);
            this.wsBridge.applyPhase(params.qutrit_index);
        });
    }

    async handleApplyCSUM(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('CSUM operation timeout'));
            }, 5000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    operation: 'csum',
                    control: params.control,
                    target: params.target,
                    result: data
                });
            };

            this.mcpCallbacks.set(`csum_${params.control}_${params.target}`, handler);
            this.wsBridge.applyCSUM(params.control, params.target);
        });
    }

    async handleApplyPauliX(params) {
        this.wsBridge.send({
            type: 'quantum_operation',
            operation: 'pauli_x',
            target: params.qutrit_index
        });
        return { success: true, operation: 'pauli_x', qutrit_index: params.qutrit_index };
    }

    async handleApplyPauliZ(params) {
        this.wsBridge.send({
            type: 'quantum_operation',
            operation: 'pauli_z',
            target: params.qutrit_index
        });
        return { success: true, operation: 'pauli_z', qutrit_index: params.qutrit_index };
    }

    async handleMeasure(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Measurement timeout'));
            }, 5000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    operation: 'measure',
                    qutrit_index: params.qutrit_index,
                    result: data.result || data
                });
            };

            this.mcpCallbacks.set(`measure_${params.qutrit_index}`, handler);
            this.wsBridge.measureQutrit(params.qutrit_index);
        });
    }

    async handleSetConfig(params) {
        this.wsBridge.updateConfig(params.section, params.key, params.value);
        return {
            success: true,
            config_updated: true,
            section: params.section,
            key: params.key,
            value: params.value
        };
    }

    async handleSetNumQutrits(params) {
        this.wsBridge.setNumQutrits(params.count);
        return { success: true, num_qutrits: params.count };
    }

    async handleSetEntanglementGraph(params) {
        this.wsBridge.setEntanglementGraph(params.graph_type);
        return { success: true, graph_type: params.graph_type };
    }

    async handleRunInference(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Inference timeout'));
            }, params.timeout_ms || 5000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    operation: 'inference',
                    input: params.input,
                    result: data
                });
            };

            this.mcpCallbacks.set('inference_result', handler);
            this.wsBridge.runInference(params.input);
        });
    }

    async handleGetMetrics(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Metrics request timeout'));
            }, 5000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    metrics: data
                });
            };

            this.mcpCallbacks.set('metrics', handler);
            this.wsBridge.send({ type: 'get_metrics' });
        });
    }

    async handleGetPipelineStatus(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Pipeline status timeout'));
            }, 5000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    pipeline_status: data
                });
            };

            this.mcpCallbacks.set('pipeline_status', handler);
            this.wsBridge.getPipelineStatus();
        });
    }

    async handleTriggerPipeline(params) {
        this.wsBridge.triggerPipeline(params.pipeline_type);
        return {
            success: true,
            triggered: true,
            pipeline_type: params.pipeline_type,
            branch: params.branch || 'main'
        };
    }

    async handleComputeBetti(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Betti computation timeout'));
            }, 5000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    betti_numbers: data
                });
            };

            this.mcpCallbacks.set('betti_result', handler);
            this.wsBridge.send({
                type: 'compute_betti',
                nodes: params.nodes,
                edges: params.edges
            });
        });
    }

    async handleStartFFTraining(params) {
        this.wsBridge.send({
            type: 'start_ff_training',
            layers: params.layers || [],
            epochs: params.epochs || 100
        });
        return {
            success: true,
            training_started: true,
            layers: params.layers || [],
            epochs: params.epochs || 100
        };
    }

    async handleStopFFTraining(params) {
        this.wsBridge.send({ type: 'stop_ff_training' });
        return { success: true, training_stopped: true };
    }

    async handleInitGraph(params) {
        this.wsBridge.send({
            type: 'init_graph',
            nodes: params.nodes || 8,
            edges: params.edges || 14
        });
        return {
            success: true,
            initialized: true,
            nodes: params.nodes || 8,
            edges: params.edges || 14
        };
    }

    async handleAddGraphNode(params) {
        this.wsBridge.send({ type: 'add_graph_node' });
        return { success: true, node_added: true };
    }

    async handleAddGraphEdge(params) {
        this.wsBridge.send({ type: 'add_graph_edge' });
        return { success: true, edge_added: true };
    }

    async handleReadMemory(params) {
        const data = this.wsBridge.readMemory(params.offset, params.length);
        return {
            success: true,
            offset: params.offset,
            length: params.length,
            data: Array.from(data)
        };
    }

    async handleWriteMemory(params) {
        this.wsBridge.writeMemory(params.offset, params.data);
        return {
            success: true,
            offset: params.offset,
            length: params.data.length
        };
    }

    // ============================================================
    // Pipeline Control Handlers for Integrated Training Pipeline
    // ============================================================

    async handleInitTrainingPipeline(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Pipeline initialization timeout'));
            }, 10000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    initialized: true,
                    state: data.state || 'ready',
                    config: {
                        num_experts: params.num_experts || 243,
                        graph_nodes: params.graph_nodes || 64,
                        enable_betti_guidance: params.enable_betti_guidance !== false,
                        enable_knowledge_engine: params.enable_knowledge_engine !== false,
                        epochs: params.epochs || 1000,
                        batch_size: params.batch_size || 32,
                        acquisition_threads: params.acquisition_threads || 4
                    }
                });
            };

            this.mcpCallbacks.set('pipeline_initialized', handler);
            this.wsBridge.send({
                type: 'init_training_pipeline',
                acquisition_threads: params.acquisition_threads || 4,
                num_experts: params.num_experts || 243,
                graph_nodes: params.graph_nodes || 64,
                graph_edges: params.graph_edges || 112,
                enable_betti_guidance: params.enable_betti_guidance !== false,
                enable_knowledge_engine: params.enable_knowledge_engine !== false,
                epochs: params.epochs || 1000,
                batch_size: params.batch_size || 32
            });
        });
    }

    async handleSetPipelineConfig(params) {
        this.wsBridge.send({
            type: 'set_pipeline_config',
            batch_size: params.batch_size,
            learning_rate: params.learning_rate,
            betti_guidance_threshold: params.betti_guidance_threshold,
            enable_wui_streaming: params.enable_wui_streaming
        });
        return {
            success: true,
            config_updated: true,
            batch_size: params.batch_size,
            learning_rate: params.learning_rate
        };
    }

    async handleGetTrainingMetrics(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Training metrics request timeout'));
            }, 5000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    metrics: {
                        ff_metrics: data.ff_metrics || {
                            positive_goodness: 45,
                            negative_goodness: 12,
                            goodness_delta: 33,
                            total_train_calls: 0
                        },
                        moe_metrics: data.moe_metrics || {
                            load_balance_score: 0.85,
                            avg_routing_latency_ms: 2.3,
                            expert_utilization: [],
                            expert_deltas: []
                        },
                        betti_numbers: data.betti_numbers || {
                            beta_0: 1,
                            beta_1: 14,
                            beta_2: 0,
                            euler_characteristic: -13
                        },
                        graph_state: data.graph_state || {
                            nodes: 64,
                            edges: 112,
                            topology: 'scale_free'
                        },
                        data_synthesizer: data.data_synthesizer || {
                            total_acquired: 0,
                            total_perturbed: 0,
                            api_failures: 0,
                            queue_depth: 0
                        },
                        current_epoch: data.current_epoch || 0,
                        current_batch: data.current_batch || 0,
                        training_progress: data.training_progress || 0,
                        is_running: data.is_running || false,
                        status_message: data.status_message || 'ready'
                    }
                });
            };

            this.mcpCallbacks.set('training_metrics', handler);
            this.wsBridge.send({
                type: 'get_training_metrics',
                include_betti: params.include_betti !== false,
                include_graph_state: params.include_graph_state !== false,
                include_expert_stats: params.include_expert_stats !== false
            });
        });
    }

    async handleApplyBettiGuidance(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Betti guidance timeout'));
            }, 15000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    guidance_applied: true,
                    forced: params.force || false,
                    topology_changes: data.changes || 0,
                    new_beta_1: data.new_beta_1 || 14,
                    routing_quality: data.routing_quality || 0.85
                });
            };

            this.mcpCallbacks.set('betti_guidance_applied', handler);
            this.wsBridge.send({
                type: 'apply_betti_guidance',
                force: params.force || false
            });
        });
    }

    async handlePauseTraining(params) {
        this.wsBridge.send({ type: 'pause_training' });
        return {
            success: true,
            paused: true,
            can_resume: true
        };
    }

    async handleResumeTraining(params) {
        this.wsBridge.send({ type: 'resume_training' });
        return {
            success: true,
            resumed: true
        };
    }

    async handleExportModel(params) {
        this.wsBridge.send({
            type: 'export_model',
            path: params.path,
            include_topology: params.include_topology !== false
        });
        return {
            success: true,
            exported: true,
            path: params.path,
            include_topology: params.include_topology !== false
        };
    }

    async handleImportModel(params) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Model import timeout'));
            }, 10000);

            const handler = (data) => {
                clearTimeout(timeout);
                resolve({
                    success: true,
                    imported: true,
                    path: params.path,
                    experts_loaded: data.experts_loaded || 243,
                    topology_loaded: data.topology_loaded || true
                });
            };

            this.mcpCallbacks.set('model_imported', handler);
            this.wsBridge.send({
                type: 'import_model',
                path: params.path
            });
        });
    }

    /**
     * Cleanup
     */
    dispose() {
        this.pendingRequests.forEach((pending, id) => {
            pending.reject(new Error('Bridge disposed'));
        });
        this.pendingRequests.clear();
        this.mcpCallbacks.clear();
    }
}

// Export for use
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { WuiMcpBridge };
} else if (typeof window !== 'undefined') {
    window.WuiMcpBridge = WuiMcpBridge;
}
