#include "init_daemon.hpp"
#include <iostream>
#include <chrono>
#include <algorithm>

#ifdef __linux__
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#endif

namespace q_mini_incus::runtime::qminid {

InitDaemon::InitDaemon(bool container_mode, bool unified_mode)
    : container_mode_(container_mode)
    , unified_mode_(unified_mode)
    , start_time_(std::chrono::steady_clock::now()) {
}

InitDaemon::~InitDaemon() {
    Shutdown();
    CleanupServices();
}

bool InitDaemon::Initialize() {
    std::cout << "[qminid] Initializing ternary-native init daemon...\n";
    
    if (IsPid1()) {
        std::cout << "[qminid] Running as PID 1 in ternary-native namespace\n";
    }
    
    // Initialize IPC manager
    ipc_manager_ = ipc::CreateTernaryIPCManager();
    if (!ipc_manager_->Initialize(container_mode_)) {
        std::cerr << "[qminid] Failed to initialize IPC manager\n";
        return false;
    }
    
    // Set up message handlers
    ipc_manager_->SetMessageHandler(
        [this](const ipc::TernaryMessage& msg) { OnTernaryMessage(msg); });
    
    // In unified mode, services run in-process
    if (unified_mode_) {
        std::cout << "[qminid] Unified mode: services will run in-process\n";
    }
    
    std::cout << "[qminid] Initialization complete\n";
    return true;
}

void InitDaemon::Run() {
    std::cout << "[qminid] Starting main event loop\n";
    
    // Start service monitoring thread
    std::thread monitor_thread(&InitDaemon::MonitorServices, this);
    
    // Main event loop
    while (!shutdown_requested_) {
        // Process IPC messages (non-blocking)
        ipc_manager_->PollMessages(std::chrono::milliseconds(100));
        
        // Update statistics
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            auto now = std::chrono::steady_clock::now();
            stats_.uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                now - start_time_).count();
        }
        
        // Small sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "[qminid] Shutdown requested, stopping services...\n";
    
    StopAllServices();
    monitor_thread.join();
    
    std::cout << "[qminid] Shutdown complete\n";
}

void InitDaemon::Shutdown() {
    std::lock_guard<std::mutex> lock(shutdown_mutex_);
    shutdown_requested_ = true;
    shutdown_cv_.notify_all();
}

void InitDaemon::RegisterService(const ServiceConfig& config) {
    std::lock_guard<std::mutex> lock(services_mutex_);
    
    auto instance = std::make_unique<ServiceInstance>();
    instance->config = config;
    instance->state = ServiceState::STOPPED;
    
    services_[config.name] = std::move(instance);
    
    std::cout << "[qminid] Registered service: " << config.name << "\n";
}

bool InitDaemon::StartService(const std::string& name) {
    std::lock_guard<std::mutex> lock(services_mutex_);
    
    auto it = services_.find(name);
    if (it == services_.end()) {
        std::cerr << "[qminid] Service not found: " << name << "\n";
        return false;
    }
    
    auto* service = it->second.get();
    if (service->state == ServiceState::RUNNING) {
        std::cout << "[qminid] Service already running: " << name << "\n";
        return true;
    }
    
    service->state = ServiceState::STARTING;
    
    if (unified_mode_) {
        // In unified mode, run in-process
        service->service_thread = std::thread(&InitDaemon::RunService, this, service);
    } else {
        // In container mode, would spawn separate process
        // TODO: Implement process spawning for multi-container mode
        service->service_thread = std::thread(&InitDaemon::RunService, this, service);
    }
    
    std::cout << "[qminid] Started service: " << name << "\n";
    return true;
}

bool InitDaemon::StopService(const std::string& name) {
    std::lock_guard<std::mutex> lock(services_mutex_);
    
    auto it = services_.find(name);
    if (it == services_.end()) {
        return false;
    }
    
    auto* service = it->second.get();
    if (service->state != ServiceState::RUNNING) {
        return true;
    }
    
    service->state = ServiceState::STOPPING;
    
    // Send shutdown message via ternary IPC
    ipc::TernaryMessage shutdown_msg;
    shutdown_msg.type = ipc::MessageType::SHUTDOWN;
    ipc_manager_->SendMessage(service->ipc_endpoint, shutdown_msg);
    
    // Wait for service thread to finish
    if (service->service_thread.joinable()) {
        service->service_thread.join();
    }
    
    service->state = ServiceState::STOPPED;
    std::cout << "[qminid] Stopped service: " << name << "\n";
    
    return true;
}

ServiceState InitDaemon::GetServiceState(const std::string& name) const {
    std::lock_guard<std::mutex> lock(services_mutex_);
    
    auto it = services_.find(name);
    if (it == services_.end()) {
        return ServiceState::ERROR;
    }
    
    return it->second->state;
}

bool InitDaemon::IsPid1() const {
#ifdef __linux__
    return getpid() == 1;
#else
    return false;  // Windows/Mac development fallback
#endif
}

InitDaemon::DaemonStats InitDaemon::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    DaemonStats stats = stats_;
    
    // Update current values
    auto now = std::chrono::steady_clock::now();
    stats.uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now - start_time_).count();
    
    {
        std::lock_guard<std::mutex> svc_lock(services_mutex_);
        stats.active_services = std::count_if(
            services_.begin(), services_.end(),
            [](const auto& pair) {
                return pair.second->state == ServiceState::RUNNING;
            });
    }
    
    return stats;
}

void InitDaemon::RunService(ServiceInstance* service) {
    service->state = ServiceState::RUNNING;
    
    std::cout << "[qminid] Service " << service->config.name << " is running\n";
    
    // Service main loop
    while (service->state == ServiceState::RUNNING && !shutdown_requested_) {
        // Service would do its work here
        // For now, just simulate activity
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    service->state = ServiceState::STOPPED;
}

void InitDaemon::MonitorServices() {
    while (!shutdown_requested_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        if (shutdown_requested_) break;
        
        std::lock_guard<std::mutex> lock(services_mutex_);
        
        for (auto& [name, service] : services_) {
            if (service->state == ServiceState::ERROR && service->config.auto_restart) {
                if (service->restart_count < service->config.restart_max_attempts) {
                    std::cout << "[qminid] Restarting service: " << name << "\n";
                    HandleServiceRestart(service.get());
                }
            }
        }
    }
}

void InitDaemon::HandleServiceRestart(ServiceInstance* service) {
    service->restart_count++;
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.services_restarted++;
    }
    
    service->state = ServiceState::RESTARTING;
    
    if (service->service_thread.joinable()) {
        service->service_thread.join();
    }
    
    // Restart the service
    service->service_thread = std::thread(&InitDaemon::RunService, this, service);
}

void InitDaemon::OnTernaryMessage(const ipc::TernaryMessage& msg) {
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_processed++;
    }
    
    // Route message to appropriate service or handle internally
    switch (msg.type) {
        case ipc::MessageType::SERVICE_CONTROL:
            // Handle service control messages
            break;
        case ipc::MessageType::BOUNDARY_CONVERSION:
            // Handle namespace boundary conversion
            break;
        default:
            // Pass to destination service
            break;
    }
}

void InitDaemon::OnBoundaryConversion(const ipc::TernaryMessage& ternary_side,
                                      const std::vector<uint8_t>& binary_side) {
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.boundary_conversions++;
    }
    
    // This is the ONLY place where ternary-to-binary conversion happens
    // All external I/O goes through this boundary
}

void InitDaemon::StopAllServices() {
    std::lock_guard<std::mutex> lock(services_mutex_);
    
    for (auto& [name, service] : services_) {
        if (service->state == ServiceState::RUNNING) {
            service->state = ServiceState::STOPPING;
            
            // Send shutdown message
            ipc::TernaryMessage shutdown_msg;
            shutdown_msg.type = ipc::MessageType::SHUTDOWN;
            ipc_manager_->SendMessage(service->ipc_endpoint, shutdown_msg);
        }
    }
}

void InitDaemon::CleanupServices() {
    std::lock_guard<std::mutex> lock(services_mutex_);
    
    for (auto& [name, service] : services_) {
        if (service->service_thread.joinable()) {
            service->service_thread.join();
        }
    }
    
    services_.clear();
}

std::unique_ptr<InitDaemon> CreateInitDaemon(bool container_mode, bool unified_mode) {
    return std::make_unique<InitDaemon>(container_mode, unified_mode);
}

} // namespace q_mini_incus::runtime::qminid
