# Cline n8n Tool Integration Documentation

## Overview
This document outlines how Cline will integrate with n8n workflow automation as part of the VS Code toolset, following the DoD compliance and smart proxy requirements defined in the configuration.

## Integration Architecture

### 1. Python Wrapper Interface
The `cline_n8n_wrapper.py` module provides a standardized interface for:
- Workflow creation, retrieval, and management
- Execution control with rate limiting (100 requests per 15 minutes)
- Status monitoring and execution tracking
- Smart proxy orchestration through workflow nodes

### 2. Workflow Registration Process
Cline will register workflows through the following process:

#### Step 1: Workflow Discovery
```python
wrapper = ClineN8NWrapper()
workflows = wrapper.get_all_workflows()
```

#### Step 2: Smart Proxy Detection
For each workflow, Cline will analyze nodes to identify smart proxy usage:
- DMVPN/VXLAN for network tunneling
- GreyLog for log analysis
- Forman for configuration management
- Zabbix for monitoring
- Netdisco for network discovery
- Netbox for IPAM/DCIM

#### Step 3: Compliance Verification
Each workflow is validated against DoD compliance requirements:
- Keycloak authentication headers
- Role-based authorization checks
- Encryption in transit verification
- Audit trail generation

## Tool Use Registration

### 1. Workflow Execution Commands
Cline will expose the following commands:

#### Execute Workflow
```bash
n8n execute <workflow_id> [--input <json>]
```

#### Monitor Execution
```bash
n8n status <execution_id>
```

#### Create Workflow from Template
```bash
n8n create <template_name> [--params <json>]
```

### 2. Smart Proxy Integration Commands
#### DMVPN/VXLAN Tunnel Management
```bash
n8n dmvpn create <tunnel_id> [--config <json>]
n8n dmvpn status <tunnel_id>
n8n dmvpn delete <tunnel_id>
```

#### Netbox IPAM Operations
```bash
n8n netbox prefixes [--site <site>] [--limit <number>]
n8n netbox devices [--role <role>] [--status <status>]
```

#### Zabbix Monitoring
```bash
n8n zabbix triggers [--severity <level>] [--since <hours>]
n8n zabbix problems [--host <hostname>]
```

### 3. Edge Computing Commands
#### Low Bandwidth Mode
```bash
n8n edge compress <data> [--level <1-9>]
n8n edge decompress <data>
```

#### Austere Conditions
```bash
n8n edge failover <workflow_id> [--target <backup>]
n8n edge resilience check <workflow_id>
```

## API Integration Examples

### Example 1: IT/OT Integration Workflow
```python
# Using the sample Netbox workflow
wrapper = ClineN8NWrapper()
workflow = wrapper.create_workflow(sample_netbox_workflow)
execution = wrapper.execute_workflow(workflow['id'], {
    "site": "main-site",
    "limit": 50
})

# Monitor execution
status = wrapper.get_execution_status(execution['id'])
while status['status'] not in ['COMPLETED', 'FAILED']:
    status = wrapper.get_execution_status(execution['id'])
    time.sleep(2)
```

### Example 2: Smart Proxy Orchestration
```python
# DMVPN tunnel creation for secure workflow execution
tunnel_id = wrapper.dmvpn_create({
    "name": "secure-tunnel",
    "endpoint": "10.0.0.1",
    "encryption": "aes-256"
})

# Execute workflow through secure tunnel
workflow_execution = wrapper.execute_workflow(
    workflow_id="itot-integration",
    input_data={"tunnel_id": tunnel_id}
)
```

## Compliance and Security

### 1. Authentication Flow
All API calls include:
- Keycloak JWT tokens
- Role-based access control headers
- CSRF protection tokens
- Audit logging for all operations

### 2. Data Protection
- Encryption in transit (TLS 1.3)
- Data classification metadata
- Secure key management
- Compliance monitoring

### 3. Resilience Features
- High availability with automatic failover
- Disaster recovery workflows
- Automated backup scheduling
- Edge computing fallback modes

## Usage Guidelines

### Rate Limiting
Respect the 100 requests per 15 minutes limit:
- Batch operations when possible
- Use workflow execution queuing
- Implement exponential backoff for retries

### Smart Proxy Best Practices
- Use DMVPN for secure network communication
- Leverage GreyLog for centralized logging
- Utilize Netbox for IPAM and DCIM operations
- Implement Zabbix for proactive monitoring

### Edge Computing Considerations
- Enable compression for low bandwidth scenarios
- Use offline mode for austere conditions
- Implement intelligent data sync
- Configure automatic failover

## Monitoring and Logging

### Execution Tracking
- Workflow execution history
- Performance metrics collection
- Error rate monitoring
- Resource utilization tracking

### Audit Trail
- All workflow creations and modifications
- Execution attempts and results
- User access and authorization events
- Compliance status reporting

## Future Enhancements

### Planned Features
- AI-powered workflow optimization
- Predictive alerting and anomaly detection
- Automated workflow generation
- Enhanced edge computing capabilities

### Integration Roadmap
- Additional smart proxy support
- Advanced compliance reporting
- Real-time collaboration features
- Mobile workflow management