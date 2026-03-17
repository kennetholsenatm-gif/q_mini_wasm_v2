-- JIT Service Registry Database Schema
-- PostgreSQL database for tracking container lifecycle and resource allocation

-- Services table
CREATE TABLE services (
    id SERIAL PRIMARY KEY,
    service_id VARCHAR(255) UNIQUE NOT NULL,
    service_name VARCHAR(255) NOT NULL,
    service_type VARCHAR(100) NOT NULL,
    container_name VARCHAR(255),
    container_id VARCHAR(255),
    status VARCHAR(50) DEFAULT 'stopped',
    cpu_cores INTEGER DEFAULT 2,
    memory_gb INTEGER DEFAULT 2,
    gpu_required BOOLEAN DEFAULT false,
    always_on BOOLEAN DEFAULT false,
    dependencies JSONB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_start_time TIMESTAMP,
    last_stop_time TIMESTAMP,
    restart_count INTEGER DEFAULT 0,
    health_check_url VARCHAR(500),
    health_status VARCHAR(50) DEFAULT 'unknown',
    health_last_checked TIMESTAMP
);

-- Resource allocation table
CREATE TABLE resource_allocations (
    id SERIAL PRIMARY KEY,
    service_id INTEGER REFERENCES services(id),
    cpu_allocated INTEGER NOT NULL,
    memory_allocated_gb INTEGER NOT NULL,
    gpu_allocated BOOLEAN DEFAULT false,
    allocation_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    deallocation_time TIMESTAMP,
    status VARCHAR(50) DEFAULT 'active'
);

-- Service requests table
CREATE TABLE service_requests (
    id SERIAL PRIMARY KEY,
    service_id INTEGER REFERENCES services(id),
    request_type VARCHAR(50) NOT NULL, -- 'start', 'stop', 'restart'
    requested_by VARCHAR(255),
    request_source VARCHAR(255), -- 'n8n', 'api', 'manual'
    request_data JSONB,
    status VARCHAR(50) DEFAULT 'pending',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    completed_at TIMESTAMP,
    error_message TEXT
);

-- Resource monitoring table
CREATE TABLE resource_monitoring (
    id SERIAL PRIMARY KEY,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    total_cpu_cores INTEGER DEFAULT 12,
    total_memory_gb INTEGER DEFAULT 68,
    total_gpu_memory_gb INTEGER DEFAULT 2,
    allocated_cpu_cores INTEGER DEFAULT 0,
    allocated_memory_gb INTEGER DEFAULT 0,
    allocated_gpu_memory_gb INTEGER DEFAULT 0,
    active_services INTEGER DEFAULT 0,
    system_cpu_usage DECIMAL(5,2) DEFAULT 0.0,
    system_memory_usage_gb DECIMAL(8,2) DEFAULT 0.0
);

-- Service events table
CREATE TABLE service_events (
    id SERIAL PRIMARY KEY,
    service_id INTEGER REFERENCES services(id),
    event_type VARCHAR(100) NOT NULL,
    event_data JSONB,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    source VARCHAR(100)
);

-- Indexes for performance
CREATE INDEX idx_services_status ON services(status);
CREATE INDEX idx_services_always_on ON services(always_on);
CREATE INDEX idx_services_service_type ON services(service_type);
CREATE INDEX idx_resource_allocations_service_id ON resource_allocations(service_id);
CREATE INDEX idx_resource_allocations_status ON resource_allocations(status);
CREATE INDEX idx_service_requests_status ON service_requests(status);
CREATE INDEX idx_service_requests_created_at ON service_requests(created_at);
CREATE INDEX idx_resource_monitoring_timestamp ON resource_monitoring(timestamp);

-- Functions for resource management
CREATE OR REPLACE FUNCTION get_available_resources()
RETURNS TABLE(
    available_cpu_cores INTEGER,
    available_memory_gb INTEGER,
    available_gpu_memory_gb INTEGER,
    total_cpu_cores INTEGER,
    total_memory_gb INTEGER,
    total_gpu_memory_gb INTEGER
)
AS $$
BEGIN
    RETURN QUERY
    SELECT 
        (SELECT total_cpu_cores FROM resource_monitoring ORDER BY timestamp DESC LIMIT 1) - 
        COALESCE((SELECT SUM(cpu_allocated) FROM resource_allocations WHERE status = 'active'), 0) AS available_cpu_cores,
        (SELECT total_memory_gb FROM resource_monitoring ORDER BY timestamp DESC LIMIT 1) - 
        COALESCE((SELECT SUM(memory_allocated_gb) FROM resource_allocations WHERE status = 'active'), 0) AS available_memory_gb,
        (SELECT total_gpu_memory_gb FROM resource_monitoring ORDER BY timestamp DESC LIMIT 1) - 
        COALESCE((SELECT SUM(gpu_allocated::int) FROM resource_allocations WHERE status = 'active' AND gpu_allocated = true), 0) AS available_gpu_memory_gb,
        (SELECT total_cpu_cores FROM resource_monitoring ORDER BY timestamp DESC LIMIT 1) AS total_cpu_cores,
        (SELECT total_memory_gb FROM resource_monitoring ORDER BY timestamp DESC LIMIT 1) AS total_memory_gb,
        (SELECT total_gpu_memory_gb FROM resource_monitoring ORDER BY timestamp DESC LIMIT 1) AS total_gpu_memory_gb;
END;
$$ LANGUAGE plpgsql;

-- Function to check if service can be started
CREATE OR REPLACE FUNCTION can_start_service(p_service_id INTEGER)
RETURNS BOOLEAN
AS $$
DECLARE
    v_service_record RECORD;
    v_available_resources RECORD;
    v_current_allocations INTEGER;
BEGIN
    -- Get service requirements
    SELECT * INTO v_service_record FROM services WHERE id = p_service_id;
    
    -- Get available resources
    SELECT * INTO v_available_resources FROM get_available_resources();
    
    -- Check if service is always on
    IF v_service_record.always_on THEN
        RETURN true;
    END IF;
    
    -- Check CPU availability
    IF v_service_record.cpu_cores > v_available_resources.available_cpu_cores THEN
        RETURN false;
    END IF;
    
    -- Check memory availability
    IF v_service_record.memory_gb > v_available_resources.available_memory_gb THEN
        RETURN false;
    END IF;
    
    -- Check GPU availability
    IF v_service_record.gpu_required AND v_service_record.gpu_required > v_available_resources.available_gpu_memory_gb THEN
        RETURN false;
    END IF;
    
    RETURN true;
END;
$$ LANGUAGE plpgsql;

-- Function to update service status
CREATE OR REPLACE FUNCTION update_service_status(p_service_id INTEGER, p_status VARCHAR(50))
RETURNS VOID
AS $$
BEGIN
    UPDATE services 
    SET status = p_status, updated_at = CURRENT_TIMESTAMP
    WHERE id = p_service_id;
END;
$$ LANGUAGE plpgsql;

-- Insert initial resource monitoring record
INSERT INTO resource_monitoring (
    total_cpu_cores, total_memory_gb, total_gpu_memory_gb,
    allocated_cpu_cores, allocated_memory_gb, allocated_gpu_memory_gb,
    active_services
) VALUES (12, 68, 2, 0, 0, 0, 0);

-- Insert DevEnvironment services (always-on)
INSERT INTO services (
    service_id, service_name, service_type, always_on, 
    cpu_cores, memory_gb, dependencies, health_check_url
) VALUES 
('dev-n8n', 'n8n-orchestrator', 'workflow', true, 2, 2, '[]', 'http://n8n-orchestrator:5678/health'),
('dev-bitnet', 'bitnet-llm', 'ai', true, 4, 8, '["dev-n8n"]', 'http://bitnet-llm:8000/health'),
('dev-monitoring', 'prometheus-grafana', 'monitoring', true, 2, 4, '[]', 'http://prometheus:9090/-/healthy'),
('dev-logging', 'fluentbit-logging', 'logging', true, 1, 2, '[]', 'http://fluentbit:2020/ping');