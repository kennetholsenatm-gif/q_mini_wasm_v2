#!/usr/bin/env python3
"""
JIT Resource Manager
Manages container lifecycle and resource allocation for JIT Docker instances
"""

import asyncio
import json
import logging
import time
import docker
import psutil
from datetime import datetime, timedelta
from typing import Dict, List, Optional, Tuple
import psycopg2
from psycopg2.extras import RealDictCursor
import requests
import os

# Configuration
DB_CONFIG = {
    'host': 'postgres-server',
    'database': 'jit_registry',
    'user': 'jit_user',
    'password': 'jit_password'
}

HARDWARE_CONFIG = {
    'total_cpu_cores': 12,
    'total_memory_gb': 68,
    'total_gpu_memory_gb': 2,
    'conservative_startup_time': 30,  # seconds
    'conservative_shutdown_time': 15,  # seconds
    'idle_timeout_minutes': 10,
    'health_check_interval': 30,  # seconds
    'resource_monitoring_interval': 60  # seconds
}

class ResourceManager:
    def __init__(self):
        self.docker_client = docker.from_env()
        self.db_connection = None
        self.running = True
        self.logger = self._setup_logging()
        
    def _setup_logging(self):
        """Setup logging configuration"""
        logging.basicConfig(
            level=logging.INFO,
            format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
            handlers=[
                logging.FileHandler('/var/log/jit-resource-manager.log'),
                logging.StreamHandler()
            ]
        )
        return logging.getLogger(__name__)
    
    def _get_db_connection(self):
        """Get database connection"""
        if not self.db_connection or self.db_connection.closed:
            self.db_connection = psycopg2.connect(**DB_CONFIG)
        return self.db_connection
    
    async def monitor_resources(self):
        """Monitor system resources and update database"""
        while self.running:
            try:
                # Get system metrics
                cpu_usage = psutil.cpu_percent(interval=1)
                memory_info = psutil.virtual_memory()
                memory_usage_gb = memory_info.used / (1024**3)
                
                # Get active containers
                active_containers = self.docker_client.containers.list(
                    filters={'status': 'running'}
                )
                
                # Calculate allocated resources
                allocated_cpu = 0
                allocated_memory = 0
                allocated_gpu = 0
                
                for container in active_containers:
                    try:
                        stats = container.stats(stream=False)
                        # Calculate CPU usage
                        cpu_delta = stats['cpu_stats']['cpu_usage']['total_usage'] - \
                                   stats['precpu_stats']['cpu_usage']['total_usage']
                        system_delta = stats['cpu_stats']['system_cpu_usage'] - \
                                      stats['precpu_stats']['system_cpu_usage']
                        
                        if system_delta > 0:
                            cpu_percent = (cpu_delta / system_delta) * len(stats['cpu_stats']['cpu_usage']['percpu_usage'])
                            allocated_cpu += int(cpu_percent)
                        
                        # Calculate memory usage
                        memory_usage = stats['memory_stats']['usage'] / (1024**3)
                        allocated_memory += memory_usage
                        
                    except Exception as e:
                        self.logger.warning(f"Error getting stats for container {container.name}: {e}")
                
                # Update database
                conn = self._get_db_connection()
                cursor = conn.cursor()
                
                cursor.execute("""
                    INSERT INTO resource_monitoring (
                        system_cpu_usage, system_memory_usage_gb,
                        allocated_cpu_cores, allocated_memory_gb, active_services
                    ) VALUES (%s, %s, %s, %s, %s)
                """, (cpu_usage, memory_usage_gb, allocated_cpu, allocated_memory, len(active_containers)))
                
                conn.commit()
                cursor.close()
                
                self.logger.info(f"Resource monitoring: CPU {cpu_usage}%, Memory {memory_usage_gb:.2f}GB, "
                               f"Allocated CPU {allocated_cpu}, Allocated Memory {allocated_memory:.2f}GB")
                
            except Exception as e:
                self.logger.error(f"Error in resource monitoring: {e}")
            
            await asyncio.sleep(HARDWARE_CONFIG['resource_monitoring_interval'])
    
    async def monitor_services(self):
        """Monitor service health and manage lifecycle"""
        while self.running:
            try:
                conn = self._get_db_connection()
                cursor = conn.cursor(cursor_factory=RealDictCursor)
                
                # Get all active services
                cursor.execute("""
                    SELECT * FROM services 
                    WHERE status = 'running' AND always_on = false
                """)
                services = cursor.fetchall()
                
                current_time = datetime.now()
                
                for service in services:
                    # Check health
                    if service['health_check_url']:
                        await self._check_service_health(service, cursor)
                    
                    # Check idle timeout
                    if service['last_activity_time']:
                        idle_time = current_time - service['last_activity_time']
                        if idle_time > timedelta(minutes=HARDWARE_CONFIG['idle_timeout_minutes']):
                            self.logger.info(f"Service {service['service_name']} is idle, stopping...")
                            await self._stop_service(service['id'], cursor)
                
                # Check for pending service requests
                cursor.execute("""
                    SELECT * FROM service_requests 
                    WHERE status = 'pending' 
                    ORDER BY created_at ASC
                """)
                requests = cursor.fetchall()
                
                for request in requests:
                    await self._process_service_request(request, cursor)
                
                conn.commit()
                cursor.close()
                
            except Exception as e:
                self.logger.error(f"Error in service monitoring: {e}")
            
            await asyncio.sleep(HARDWARE_CONFIG['health_check_interval'])
    
    async def _check_service_health(self, service, cursor):
        """Check service health"""
        try:
            response = requests.get(
                service['health_check_url'],
                timeout=10
            )
            
            health_status = 'healthy' if response.status_code == 200 else 'unhealthy'
            
            cursor.execute("""
                UPDATE services 
                SET health_status = %s, health_last_checked = NOW()
                WHERE id = %s
            """, (health_status, service['id']))
            
            if health_status == 'unhealthy':
                self.logger.warning(f"Service {service['service_name']} is unhealthy")
                
        except Exception as e:
            self.logger.error(f"Health check failed for {service['service_name']}: {e}")
            cursor.execute("""
                UPDATE services 
                SET health_status = 'error', health_last_checked = NOW()
                WHERE id = %s
            """, (service['id'],))
    
    async def _process_service_request(self, request, cursor):
        """Process a service request"""
        try:
            cursor.execute("UPDATE service_requests SET status = 'processing' WHERE id = %s", (request['id'],))
            
            if request['request_type'] == 'start':
                await self._start_service(request['service_id'], cursor)
            elif request['request_type'] == 'stop':
                await self._stop_service(request['service_id'], cursor)
            elif request['request_type'] == 'restart':
                await self._restart_service(request['service_id'], cursor)
            
            cursor.execute("""
                UPDATE service_requests 
                SET status = 'completed', completed_at = NOW()
                WHERE id = %s
            """, (request['id'],))
            
        except Exception as e:
            self.logger.error(f"Error processing request {request['id']}: {e}")
            cursor.execute("""
                UPDATE service_requests 
                SET status = 'failed', error_message = %s, completed_at = NOW()
                WHERE id = %s
            """, (str(e), request['id']))
    
    async def _start_service(self, service_id: int, cursor):
        """Start a service"""
        cursor.execute("SELECT * FROM services WHERE id = %s", (service_id,))
        service = cursor.fetchone()
        
        if not service:
            raise Exception(f"Service {service_id} not found")
        
        if service['status'] == 'running':
            self.logger.info(f"Service {service['service_name']} is already running")
            return
        
        # Check if service can be started
        cursor.execute("SELECT can_start_service(%s) as can_start", (service_id,))
        can_start = cursor.fetchone()['can_start']
        
        if not can_start:
            raise Exception(f"Insufficient resources to start {service['service_name']}")
        
        # Start dependencies first
        dependencies = service.get('dependencies', [])
        for dep_id in dependencies:
            dep_cursor = self._get_db_connection().cursor()
            await self._start_service(dep_id, dep_cursor)
            dep_cursor.close()
        
        # Start the service
        compose_file = f"docker-compose.{service['service_type']}.yml"
        compose_path = f"/opt/mesh/{compose_file}"
        
        if not os.path.exists(compose_path):
            raise Exception(f"Compose file not found: {compose_path}")
        
        # Start container
        import subprocess
        result = subprocess.run([
            'docker-compose', '-f', compose_path, 'up', '-d', service['container_name']
        ], capture_output=True, text=True)
        
        if result.returncode != 0:
            raise Exception(f"Failed to start service: {result.stderr}")
        
        # Update database
        cursor.execute("""
            UPDATE services 
            SET status = 'running', container_id = %s, last_start_time = NOW()
            WHERE id = %s
        """, (result.stdout.strip(), service_id))
        
        # Record resource allocation
        cursor.execute("""
            INSERT INTO resource_allocations (service_id, cpu_allocated, memory_allocated_gb, gpu_allocated)
            VALUES (%s, %s, %s, %s)
        """, (service_id, service['cpu_cores'], service['memory_gb'], service['gpu_required']))
        
        # Record event
        cursor.execute("""
            INSERT INTO service_events (service_id, event_type, event_data, source)
            VALUES (%s, 'started', %s, 'resource-manager')
        """, (service_id, json.dumps({'container_id': result.stdout.strip()})))
        
        self.logger.info(f"Started service {service['service_name']}")
    
    async def _stop_service(self, service_id: int, cursor):
        """Stop a service"""
        cursor.execute("SELECT * FROM services WHERE id = %s", (service_id,))
        service = cursor.fetchone()
        
        if not service or service['status'] != 'running':
            self.logger.info(f"Service {service['service_name']} is not running")
            return
        
        # Stop the service
        compose_file = f"docker-compose.{service['service_type']}.yml"
        compose_path = f"/opt/mesh/{compose_file}"
        
        import subprocess
        result = subprocess.run([
            'docker-compose', '-f', compose_path, 'down', service['container_name']
        ], capture_output=True, text=True)
        
        # Update database
        cursor.execute("""
            UPDATE services 
            SET status = 'stopped', last_stop_time = NOW()
            WHERE id = %s
        """, (service_id,))
        
        # Update resource allocation
        cursor.execute("""
            UPDATE resource_allocations 
            SET status = 'deallocated', deallocation_time = NOW()
            WHERE service_id = %s AND status = 'active'
        """, (service_id,))
        
        # Record event
        cursor.execute("""
            INSERT INTO service_events (service_id, event_type, event_data, source)
            VALUES (%s, 'stopped', %s, 'resource-manager')
        """, (service_id, json.dumps({'reason': 'idle_timeout'})))
        
        self.logger.info(f"Stopped service {service['service_name']}")
    
    async def _restart_service(self, service_id: int, cursor):
        """Restart a service"""
        await self._stop_service(service_id, cursor)
        await self._start_service(service_id, cursor)
    
    async def request_service_start(self, service_id: int, requested_by: str = 'api'):
        """Request to start a service"""
        conn = self._get_db_connection()
        cursor = conn.cursor()
        
        cursor.execute("""
            INSERT INTO service_requests (service_id, request_type, requested_by, request_source)
            VALUES (%s, 'start', %s, 'api')
        """, (service_id, requested_by))
        
        conn.commit()
        cursor.close()
        
        self.logger.info(f"Service start request created for {service_id}")
    
    async def run(self):
        """Main run loop"""
        self.logger.info("Starting JIT Resource Manager")
        
        try:
            # Start monitoring tasks
            tasks = [
                asyncio.create_task(self.monitor_resources()),
                asyncio.create_task(self.monitor_services())
            ]
            
            await asyncio.gather(*tasks)
            
        except KeyboardInterrupt:
            self.logger.info("Shutting down JIT Resource Manager")
            self.running = False
        except Exception as e:
            self.logger.error(f"Error in main loop: {e}")
            raise

if __name__ == "__main__":
    manager = ResourceManager()
    asyncio.run(manager.run())