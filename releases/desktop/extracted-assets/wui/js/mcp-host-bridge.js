/**
 * MCP Host Bridge - Desktop-safe WUI transport (no WebSocket dependency)
 *
 * This bridge talks to an MCP host surface exposed by the desktop container.
 * Supported adapters:
 *   1) window.qMiniMcpHost.callTool(name, args)
 *   2) WebView2 message channel (window.chrome.webview.postMessage)
 */

(function () {
    class DirectHostAdapter {
        constructor(host) {
            this.host = host;
        }

        async callTool(name, args) {
            return this.host.callTool(name, args || {});
        }
    }

    class WebView2HostAdapter {
        constructor(webview) {
            this.webview = webview;
            this.nextId = 1;
            this.pending = new Map();

            this.webview.addEventListener('message', (event) => {
                const payload = event.data || {};
                if (!payload || payload.channel !== 'qmini-mcp-response') {
                    return;
                }

                const wait = this.pending.get(payload.id);
                if (!wait) {
                    return;
                }

                this.pending.delete(payload.id);

                if (payload.error) {
                    wait.reject(new Error(payload.error.message || String(payload.error)));
                } else {
                    wait.resolve(payload.result);
                }
            });
        }

        async callTool(name, args) {
            const id = this.nextId++;

            return new Promise((resolve, reject) => {
                const timer = setTimeout(() => {
                    this.pending.delete(id);
                    reject(new Error(`MCP tool timeout: ${name}`));
                }, 10000);

                this.pending.set(id, {
                    resolve: (value) => {
                        clearTimeout(timer);
                        resolve(value);
                    },
                    reject: (error) => {
                        clearTimeout(timer);
                        reject(error);
                    },
                });

                this.webview.postMessage({
                    channel: 'qmini-mcp-request',
                    id,
                    name,
                    arguments: args || {},
                });
            });
        }
    }

    function resolveAdapter() {
        const directCandidates = [
            globalThis.qMiniMcpHost,
            globalThis.qminiMcpHost,
            globalThis.qMiniHost,
        ];

        for (const candidate of directCandidates) {
            if (candidate && typeof candidate.callTool === 'function') {
                return new DirectHostAdapter(candidate);
            }
        }

        if (globalThis.chrome && globalThis.chrome.webview && typeof globalThis.chrome.webview.postMessage === 'function') {
            return new WebView2HostAdapter(globalThis.chrome.webview);
        }

        return null;
    }

    class MCPHostBridge {
        constructor(adapter) {
            this.adapter = adapter;
            this.callbacks = new Map();
            this.isConnected = false;
            this.metricsInterval = null;
            this.pollingMs = 5000;
            this.connectionInfo = {
                strict_mode: true,
                backend_passthrough: false,
                pipeline_initialized: false,
            };
        }

        async init() {
            if (!this.adapter) {
                this.emitError(new Error('No desktop MCP host adapter detected'));
                return;
            }

            try {
                const info = await this.callTool('wui_connect', { host: 'localhost', port: 8080, timeout_ms: 5000 });
                this.connectionInfo = {
                    strict_mode: info?.strict_mode !== false,
                    backend_passthrough: !!info?.backend_passthrough,
                    pipeline_initialized: !!info?.pipeline_initialized,
                };
                this.isConnected = true;
                globalThis.dispatchEvent(new CustomEvent('engineConnected', { detail: { ...(info || {}) } }));

                // Always publish startup readiness snapshots so UI can expose
                // train→deploy operator preflight state immediately.
                this.send({ type: 'system_self_check' }).catch(() => {});
                this.send({ type: 'desktop_shell_handshake' }).catch(() => {});
                this.send({ type: 'get_release_status' }).catch(() => {});
                this.send({ type: 'get_ops_snapshot' }).catch(() => {});

                if (this.connectionInfo.backend_passthrough || this.connectionInfo.pipeline_initialized) {
                    this.startPolling();
                }
            } catch (error) {
                this.isConnected = false;
                this.emitError(error);
                globalThis.dispatchEvent(new CustomEvent('engineDisconnected'));
            }
        }

        async callTool(name, args) {
            if (!this.adapter) {
                throw new Error('MCP host adapter unavailable');
            }

            const result = await this.adapter.callTool(name, args || {});
            return result && typeof result === 'object' && Object.keys(result).length === 1 && 'result' in result
                ? result.result
                : result;
        }

        registerHandler(type, callback) {
            this.callbacks.set(type, callback);
        }

        emitHandler(type, payload) {
            const cb = this.callbacks.get(type);
            if (cb) {
                cb(payload);
            }
        }

        emitError(error) {
            const message = error?.message || String(error);
            console.error('[MCPHostBridge]', message);
            globalThis.dispatchEvent(new CustomEvent('engineError', { detail: { message } }));
        }

        canUseRuntimeMetrics() {
            return !!(this.connectionInfo.backend_passthrough || this.connectionInfo.pipeline_initialized);
        }

        dispatchMappedEvent(type, payload) {
            switch (type) {
                case 'measurement_result':
                    globalThis.dispatchEvent(new CustomEvent('quantumMeasurement', { detail: payload }));
                    break;
                case 'metrics':
                    globalThis.dispatchEvent(new CustomEvent('metricsUpdate', { detail: payload }));
                    break;
                case 'training_metrics':
                    globalThis.dispatchEvent(new CustomEvent('trainingMetricsUpdate', { detail: payload.metrics || payload }));
                    break;
                case 'betti_result':
                    globalThis.dispatchEvent(new CustomEvent('bettiUpdate', { detail: payload }));
                    break;
                case 'pipeline_status':
                    globalThis.dispatchEvent(new CustomEvent('pipelineStatusUpdate', { detail: payload }));
                    break;
                case 'system_toml':
                    globalThis.dispatchEvent(new CustomEvent('systemTomlLoaded', { detail: payload }));
                    break;
                case 'system_toml_validation':
                    globalThis.dispatchEvent(new CustomEvent('systemTomlValidated', { detail: payload }));
                    break;
                case 'system_toml_saved':
                    globalThis.dispatchEvent(new CustomEvent('systemTomlSaved', { detail: payload }));
                    break;
                case 'system_self_check':
                    globalThis.dispatchEvent(new CustomEvent('systemSelfCheck', { detail: payload }));
                    break;
                case 'desktop_shell_handshake':
                    globalThis.dispatchEvent(new CustomEvent('desktopShellHandshake', { detail: payload }));
                    break;
                case 'release_status':
                    globalThis.dispatchEvent(new CustomEvent('releaseStatusUpdate', { detail: payload }));
                    break;
                case 'ops_snapshot':
                    globalThis.dispatchEvent(new CustomEvent('opsSnapshotUpdate', { detail: payload }));
                    break;
                default:
                    break;
            }

            this.emitHandler(type, payload);
        }

        async send(message) {
            if (!message || typeof message !== 'object') {
                return;
            }

            if (!this.isConnected && message.type !== 'connect') {
                this.emitError(new Error('Bridge is not connected to MCP host'));
                return;
            }

            try {
                switch (message.type) {
                    case 'register':
                        return;

                    case 'quantum_operation': {
                        if (!this.connectionInfo.backend_passthrough) {
                            throw new Error('Quantum operations require compiled backend passthrough in strict mode');
                        }

                        const operation = String(message.operation || '').toLowerCase();
                        const target = Number(message.target || 0);

                        if (operation === 'hadamard') {
                            const out = await this.callTool('wui_apply_hadamard', { qutrit_index: target, await_result: true });
                            this.dispatchMappedEvent('quantum_operation_complete', out);
                            return;
                        }
                        if (operation === 'phase') {
                            const out = await this.callTool('wui_apply_phase', { qutrit_index: target, await_result: true });
                            this.dispatchMappedEvent('quantum_operation_complete', out);
                            return;
                        }
                        if (operation === 'csum') {
                            const out = await this.callTool('wui_apply_csum', {
                                control: Number(message.control || 0),
                                target,
                                await_result: true,
                            });
                            this.dispatchMappedEvent('quantum_operation_complete', out);
                            return;
                        }
                        if (operation === 'measure') {
                            const out = await this.callTool('wui_measure', { qutrit_index: target, await_result: true });
                            this.dispatchMappedEvent('measurement_result', out);
                            return;
                        }
                        return;
                    }

                    case 'quantum_gate': {
                        if (!this.connectionInfo.backend_passthrough) {
                            throw new Error('Quantum gate execution requires compiled backend passthrough in strict mode');
                        }

                        const gate = String(message.gate || '').toLowerCase();
                        const qutritIndex = 0;
                        if (gate === 'hadamard') {
                            await this.callTool('wui_apply_hadamard', { qutrit_index: qutritIndex, await_result: true });
                        } else if (gate === 'phase') {
                            await this.callTool('wui_apply_phase', { qutrit_index: qutritIndex, await_result: true });
                        }
                        return;
                    }

                    case 'measure_qutrit': {
                        if (!this.connectionInfo.backend_passthrough) {
                            throw new Error('Qutrit measurement requires compiled backend passthrough in strict mode');
                        }

                        const out = await this.callTool('wui_measure', {
                            qutrit_index: Number(message.target || 0),
                            await_result: true,
                        });
                        this.dispatchMappedEvent('measurement_result', out);
                        return;
                    }

                    case 'config_update': {
                        if (typeof message.toml === 'string') {
                            const out = await this.callTool('wui_set_system_toml', { toml: message.toml });
                            this.dispatchMappedEvent('system_toml_saved', out || {});
                            return out;
                        }

                        const key = String(message.key || '');
                        if (key === 'default_num_qutrits') {
                            await this.callTool('wui_set_num_qutrits', { count: Number(message.value || 0) });
                            return;
                        }

                        if (key === 'entanglement_graph') {
                            await this.callTool('wui_set_entanglement_graph', { graph_type: String(message.value || 'linear') });
                            return;
                        }

                        await this.callTool('wui_set_config', {
                            section: message.section,
                            key: message.key,
                            value: message.value,
                        });
                        return;
                    }

                    case 'get_system_toml': {
                        const out = await this.callTool('wui_get_system_toml', {});
                        this.dispatchMappedEvent('system_toml', out);
                        return;
                    }

                    case 'validate_system_toml': {
                        const out = await this.callTool('wui_validate_system_toml', {
                            toml: typeof message.toml === 'string' ? message.toml : '',
                            path: message.path || '',
                        });
                        this.dispatchMappedEvent('system_toml_validation', out || {});
                        return out;
                    }

                    case 'set_system_toml': {
                        const out = await this.callTool('wui_set_system_toml', {
                            toml: String(message.toml || ''),
                        });
                        this.dispatchMappedEvent('system_toml_saved', out || {});
                        return out;
                    }

                    case 'system_self_check': {
                        const out = await this.callTool('wui_system_self_check', {
                            expect_artifact: message.expect_artifact || '',
                        });
                        this.dispatchMappedEvent('system_self_check', out || {});
                        return out;
                    }

                    case 'desktop_shell_handshake': {
                        const out = await this.callTool('wui_desktop_shell_handshake', {
                            artifact_path: message.artifact_path || '',
                            extract_dir: message.extract_dir || '',
                            extract_assets: message.extract_assets !== false,
                        });
                        this.dispatchMappedEvent('desktop_shell_handshake', out || {});
                        return out;
                    }

                    case 'build_release': {
                        await this.callTool('wui_build_release', {
                            output: message.output || '',
                            go_binary: message.go_binary || 'go',
                            build_bridge: message.build_bridge !== false,
                            require_native_runtime: message.require_native_runtime !== false,
                        });
                        const status = await this.callTool('wui_get_release_status', {});
                        this.dispatchMappedEvent('release_status', status || {});
                        return status;
                    }

                    case 'deploy_release': {
                        await this.callTool('wui_deploy_release', {
                            artifact_path: message.artifact_path || '',
                            target: message.target || 'local',
                            deploy_dir: message.deploy_dir || '',
                            extract_dir: message.extract_dir || '',
                            extract_assets: message.extract_assets !== false,
                            require_native_runtime: message.require_native_runtime !== false,
                        });
                        const status = await this.callTool('wui_get_release_status', {});
                        this.dispatchMappedEvent('release_status', status || {});
                        return status;
                    }

                    case 'rollback_release': {
                        await this.callTool('wui_rollback_release', {
                            deploy_dir: message.deploy_dir || '',
                            artifact_name: message.artifact_name || '',
                        });
                        const status = await this.callTool('wui_get_release_status', {});
                        this.dispatchMappedEvent('release_status', status || {});
                        return status;
                    }

                    case 'get_release_status': {
                        const out = await this.callTool('wui_get_release_status', {});
                        this.dispatchMappedEvent('release_status', out || {});
                        return out;
                    }

                    case 'get_ops_snapshot': {
                        const out = await this.callTool('wui_get_ops_snapshot', {});
                        this.dispatchMappedEvent('ops_snapshot', out || {});
                        return out;
                    }

                    case 'init_training_pipeline': {
                        const out = await this.callTool('wui_init_training_pipeline', {
                            acquisition_threads: Number(message.acquisition_threads || 4),
                            num_experts: Number(message.num_experts || 243),
                            graph_nodes: Number(message.graph_nodes || 64),
                            enable_betti_guidance: message.enable_betti_guidance !== false,
                            enable_knowledge_engine: message.enable_knowledge_engine !== false,
                            epochs: Number(message.epochs || 100),
                            batch_size: Number(message.batch_size || 32),
                        });
                        this.connectionInfo.pipeline_initialized = true;
                        this.dispatchMappedEvent('pipeline_status', out);

                        return out;
                    }

                    case 'start_ff_training': {
                        const out = await this.callTool('wui_start_ff_training', {
                            layers: message.layers || [],
                            epochs: message.epochs || 100,
                        });

                        this.connectionInfo.pipeline_initialized = true;
                        if (!this.metricsInterval) {
                            this.startPolling();
                        }

                        this.dispatchMappedEvent('pipeline_status', out);
                        return;
                    }

                    case 'stop_ff_training':
                        await this.callTool('wui_stop_ff_training', {});
                        return;

                    case 'init_graph':
                        if (!this.connectionInfo.backend_passthrough) {
                            throw new Error('Graph control requires compiled backend passthrough in strict mode');
                        }

                        await this.callTool('wui_init_graph', {
                            nodes: Number(message.nodes || 8),
                            edges: Number(message.edges || 14),
                        });
                        return;

                    case 'add_graph_node':
                        if (!this.connectionInfo.backend_passthrough) {
                            throw new Error('Graph control requires compiled backend passthrough in strict mode');
                        }

                        await this.callTool('wui_add_graph_node', {});
                        return;

                    case 'add_graph_edge':
                        if (!this.connectionInfo.backend_passthrough) {
                            throw new Error('Graph control requires compiled backend passthrough in strict mode');
                        }

                        await this.callTool('wui_add_graph_edge', {});
                        return;

                    case 'compute_betti': {
                        const out = await this.callTool('wui_compute_betti', {
                            nodes: Number(message.nodes || 0),
                            edges: Number(message.edges || 0),
                        });
                        this.dispatchMappedEvent('betti_result', out);
                        return;
                    }

                    case 'subscribe_metrics': {
                        const interval = Number(message.interval || 5000);
                        this.pollingMs = interval > 0 ? interval : 5000;
                        if (this.canUseRuntimeMetrics()) {
                            this.startPolling();
                        }
                        return;
                    }

                    case 'get_metrics': {
                        if (!this.canUseRuntimeMetrics()) {
                            throw new Error('Runtime metrics unavailable: initialize training pipeline or attach compiled backend');
                        }

                        const out = await this.callTool('wui_get_metrics', { interval_ms: this.pollingMs });
                        this.dispatchMappedEvent('metrics', out?.data || out);
                        return;
                    }

                    case 'get_training_metrics': {
                        if (!this.canUseRuntimeMetrics()) {
                            throw new Error('Training metrics unavailable: initialize training pipeline or attach compiled backend');
                        }

                        const out = await this.callTool('wui_get_training_metrics', {
                            include_betti: message.include_betti !== false,
                            include_graph_state: message.include_graph_state !== false,
                            include_expert_stats: message.include_expert_stats !== false,
                        });
                        this.dispatchMappedEvent('training_metrics', out);
                        return;
                    }

                    case 'get_pipeline_status': {
                        if (!this.canUseRuntimeMetrics()) {
                            throw new Error('Pipeline status unavailable: initialize training pipeline or attach compiled backend');
                        }

                        const out = await this.callTool('wui_get_pipeline_status', {});
                        this.dispatchMappedEvent('pipeline_status', out);
                        return;
                    }

                    default:
                        return;
                }
            } catch (error) {
                this.emitError(error);
                throw error;
            }
        }

        startPolling() {
            if (this.metricsInterval) {
                clearInterval(this.metricsInterval);
            }

            this.metricsInterval = setInterval(() => {
                if (!this.isConnected) {
                    return;
                }
                this.send({ type: 'get_metrics' }).catch(() => {});
                this.send({
                    type: 'get_training_metrics',
                    include_betti: true,
                    include_graph_state: true,
                    include_expert_stats: true,
                }).catch(() => {});
                this.send({ type: 'get_ops_snapshot' }).catch(() => {});
            }, this.pollingMs);
        }

        async disconnect() {
            try {
                await this.callTool('wui_disconnect', {});
            } catch (_) {
                // Ignore disconnect errors during teardown.
            }

            this.isConnected = false;
            if (this.metricsInterval) {
                clearInterval(this.metricsInterval);
                this.metricsInterval = null;
            }
            globalThis.dispatchEvent(new CustomEvent('engineDisconnected'));
        }

        // Compatibility helpers used by WUI MCP extension.
        applyHadamard(qutritIndex) { return this.send({ type: 'quantum_operation', operation: 'hadamard', target: qutritIndex }); }
        applyPhase(qutritIndex) { return this.send({ type: 'quantum_operation', operation: 'phase', target: qutritIndex }); }
        applyCSUM(control, target) { return this.send({ type: 'quantum_operation', operation: 'csum', control, target }); }
        measureQutrit(qutritIndex) { return this.send({ type: 'quantum_operation', operation: 'measure', target: qutritIndex }); }
        setEntanglementGraph(graphType) { return this.send({ type: 'config_update', section: 'system.qgnn', key: 'entanglement_graph', value: graphType }); }
        setNumQutrits(count) { return this.send({ type: 'config_update', section: 'system.qgnn', key: 'default_num_qutrits', value: count }); }
        runInference(input) { return this.callTool('wui_run_inference', { input: input || [] }); }
        triggerPipeline(pipelineType) { return this.callTool('wui_trigger_pipeline', { pipeline_type: pipelineType || 'local', branch: 'main' }); }
        getPipelineStatus() { return this.send({ type: 'get_pipeline_status' }); }
        updateConfig(section, key, value) { return this.send({ type: 'config_update', section, key, value }); }
        setSystemToml(toml) { return this.send({ type: 'set_system_toml', toml }); }
        validateSystemToml(toml, path = '') { return this.send({ type: 'validate_system_toml', toml, path }); }
        runSystemSelfCheck(expectArtifact = '') { return this.send({ type: 'system_self_check', expect_artifact: expectArtifact }); }
        runDesktopShellHandshake(options = {}) { return this.send({ type: 'desktop_shell_handshake', ...options }); }
        buildRelease(options = {}) { return this.send({ type: 'build_release', ...options }); }
        deployRelease(options = {}) { return this.send({ type: 'deploy_release', ...options }); }
        rollbackRelease(options = {}) { return this.send({ type: 'rollback_release', ...options }); }
        getReleaseStatus() { return this.send({ type: 'get_release_status' }); }
        getOpsSnapshot() { return this.send({ type: 'get_ops_snapshot' }); }
        readMemory(offset, length) { return this.callTool('wui_read_memory', { offset, length }); }
        writeMemory(offset, data) { return this.callTool('wui_write_memory', { offset, data }); }
    }

    document.addEventListener('DOMContentLoaded', () => {
        globalThis.wsBridge = new MCPHostBridge(resolveAdapter());
        globalThis.wsBridge.init().catch((error) => globalThis.wsBridge.emitError(error));
    });

    globalThis.MCPHostBridge = MCPHostBridge;
})();
