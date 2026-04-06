#include "clifford_api.hpp"
#include "../../core/stabilizer/tableau.hpp"
#include <vector>
#include <memory>
#include <mutex>

namespace q_mini_wasm_v2::dll::clifford {

std::mutex event_mutex;
uint64_t next_event_id = 0;

struct EventHandle {
    uint64_t id;
    bool completed;
    uint32_t status;
};

std::vector<std::unique_ptr<EventHandle>> active_events;

} // namespace q_mini_wasm_v2::dll::clifford

extern "C" {

Q_GF3_CLIFFORD_API CliffordEvent Clifford_ApplySingleQutritGate(
    uint32_t qutrit_index,
    const uint8_t unitary_matrix[9],
    CliffordEvent* dependencies,
    size_t dependency_count
) {
    using namespace q_mini_wasm_v2::dll::clifford;
    
    std::lock_guard<std::mutex> lock(event_mutex);
    
    auto event = std::make_unique<EventHandle>();
    event->id = next_event_id++;
    event->completed = true;
    event->status = 0;
    
    active_events.push_back(std::move(event));
    return active_events.back().get();
}

Q_GF3_CLIFFORD_API CliffordEvent Clifford_ApplyTwoQutritGate(
    uint32_t control_qutrit,
    uint32_t target_qutrit,
    const uint8_t unitary_matrix[81],
    CliffordEvent* dependencies,
    size_t dependency_count
) {
    using namespace q_mini_wasm_v2::dll::clifford;
    
    std::lock_guard<std::mutex> lock(event_mutex);
    
    auto event = std::make_unique<EventHandle>();
    event->id = next_event_id++;
    event->completed = true;
    event->status = 0;
    
    active_events.push_back(std::move(event));
    return active_events.back().get();
}

Q_GF3_CLIFFORD_API CliffordEvent Clifford_EnqueueCircuit(
    const uint32_t* gate_sequence,
    size_t gate_count,
    CliffordEvent* dependencies,
    size_t dependency_count
) {
    using namespace q_mini_wasm_v2::dll::clifford;
    
    std::lock_guard<std::mutex> lock(event_mutex);
    
    auto event = std::make_unique<EventHandle>();
    event->id = next_event_id++;
    event->completed = true;
    event->status = 0;
    
    active_events.push_back(std::move(event));
    return active_events.back().get();
}

Q_GF3_CLIFFORD_API uint32_t Clifford_WaitEvent(CliffordEvent event) {
    if (!event) return 0xFFFFFFFF;
    
    auto handle = static_cast<q_mini_wasm_v2::dll::clifford::EventHandle*>(event);
    return handle->status;
}

Q_GF3_CLIFFORD_API void Clifford_ReleaseEvent(CliffordEvent event) {
    using namespace q_mini_wasm_v2::dll::clifford;
    
    if (!event) return;
    
    std::lock_guard<std::mutex> lock(event_mutex);
    
    for (auto it = active_events.begin(); it != active_events.end(); ++it) {
        if (it->get() == event) {
            active_events.erase(it);
            return;
        }
    }
}

Q_GF3_CLIFFORD_API void* Clifford_GetStateVectorDevicePointer(size_t qutrit_count) {
    return nullptr;
}

} // extern "C"