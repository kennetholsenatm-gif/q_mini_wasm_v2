#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <atomic>

#include "../ipc/ternary_ipc.hpp"

namespace q_mini_incus::runtime::qminid {

/**
 * @brief Service configuration for qminid
 */
struct ServiceConfig {
    std::string name;
    std::string executable_path;
    std::vector<std::string> args;
    bool auto_restart = true;
    uint32_t restart_max_attempts = 3;
    bool enable_ternary_ipc = true;
    std::string ternary_socket_path;
};

/**
 * @brief Service state
 */
enum class ServiceState {
    STOPPED,
    STARTING,
    RUNNING,
    STOPPING,
    ERROR,
    RESTARTING
};

/**
 * @brief Ternary-native init daemon (PID 1 in Incus container)
 * 
 * qminid replaces traditional init systems in the ternary-native namespace.
 * It manages service lifecycle, provides ternary-native IPC, and handles
 * namespace boundary conversion.
 * 
 * Unlike traditional init:
 * - No UNIX signals (uses ternary message passing)
 * - No traditional syslog (ternary-native logging)
 * - Namespace boundary is the only binary conversion point
 */
class InitDaemon {
public:
    /**
     * @brief Construct init daemon
     * @param container_mode Single or multi-container deployment
     * @param unified_mode If true, run all services in one process
     */
    explicit InitDaemon(bool container_mode = false, bool unified_mode = false);
    
    /**
     * @brief Destructor - stops all services
     */
    ~InitDaemon();

    /**
     * @brief Initialize daemon (runs as PID 1)
     * @return true if initialization successful
     */
    bool Initialize();

    /**
     * @brief Run main event loop
     * Blocks until shutdown requested
     */
    void Run();

    /**
     * @brief Request graceful shutdown
     */
    void Shutdown();

    /**
     * @brief Register a service to be managed
     */
    void RegisterService(const ServiceConfig& config);

    /**
     * @brief Start a registered service
     */
    bool StartService(const std::string& name);

    /**
     * @brief Stop a running service
     */
    bool StopService(const std::string& name);

    /**
     * @brief Get service state
     */
    ServiceState GetServiceState(const std::string& name) const;

    /**
     * @brief Check if running as PID 1
     */
    bool IsPid1() const;

    /**
     * @brief Get daemon statistics
     */
    struct DaemonStats {
        uint64_t messages_processed = 0;
        uint64_t services_restarted = 0;
        uint64_t boundary_conversions = 0;
        uint64_t uptime_seconds = 0;
        size_t active_services = 0;
    };
    DaemonStats GetStats() const;

private:
    // Container configuration
    bool container_mode_;
    bool unified_mode_;
    bool shutdown_requested_{false};
    
    // Service management
    struct ServiceInstance {
        ServiceConfig config;
        ServiceState state = ServiceState::STOPPED;
        std::thread service_thread;
        std::atomic<uint32_t> restart_count{0};
        ipc::TernaryEndpoint ipc_endpoint;
    };
    
    mutable std::mutex services_mutex_;
    std::map<std::string, std::unique_ptr<ServiceInstance>> services_;
    
    // IPC
    std::unique_ptr<ipc::TernaryIPCManager> ipc_manager_;
    
    // Main thread
    std::thread main_thread_;
    std::condition_variable shutdown_cv_;
    mutable std::mutex shutdown_mutex_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    DaemonStats stats_;
    std::chrono::steady_clock::time_point start_time_;

    // Service lifecycle methods
    void RunService(ServiceInstance* service);
    void MonitorServices();
    void HandleServiceRestart(ServiceInstance* service);
    
    // IPC handlers
    void OnTernaryMessage(const ipc::TernaryMessage& msg);
    void OnBoundaryConversion(const ipc::TernaryMessage& ternary_side,
                               const std::vector<uint8_t>& binary_side);
    
    // Cleanup
    void StopAllServices();
    void CleanupServices();
};

/**
 * @brief Factory function
 */
std::unique_ptr<InitDaemon> CreateInitDaemon(bool container_mode = false, 
                                              bool unified_mode = false);

} // namespace q_mini_incus::runtime::qminid
