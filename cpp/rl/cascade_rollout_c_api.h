#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

/**
 * Foundation rollout simulator for toy cascade env.
 * Writes one return per trajectory and final state vectors.
 */
void qmw_rl_rollout_returns(std::uint64_t seed, std::size_t group_size, std::size_t max_steps,
                            std::size_t state_dim, std::size_t num_actions, float* out_returns,
                            float* out_final_states);

}
