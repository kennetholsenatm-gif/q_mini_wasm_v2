#include "cascade_rollout_c_api.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

extern "C" void qmw_rl_rollout_returns(std::uint64_t seed, std::size_t group_size, std::size_t max_steps,
                                        std::size_t state_dim, std::size_t num_actions, float* out_returns,
                                        float* out_final_states) {
  if (out_returns == nullptr || out_final_states == nullptr || group_size == 0 || state_dim == 0) {
    return;
  }
  const std::size_t actions = std::max<std::size_t>(1, num_actions);
  std::vector<std::jthread> workers;
  workers.reserve(group_size);
  for (std::size_t g = 0; g < group_size; ++g) {
    workers.emplace_back([=](std::stop_token) {
      std::mt19937_64 rng(seed ^ static_cast<std::uint64_t>(0x9E3779B185EBCA87ULL + g));
      std::normal_distribution<float> n01(0.0F, 1.0F);
      std::uniform_int_distribution<std::size_t> action_dist(0, actions - 1);

      std::vector<float> state(state_dim, 0.0F);
      for (std::size_t i = 0; i < state_dim; ++i) {
        state[i] = n01(rng);
      }
      float total_r = 0.0F;
      for (std::size_t t = 0; t < max_steps; ++t) {
        const std::size_t a = action_dist(rng);
        float sum = 0.0F;
        for (float v : state) {
          sum += v;
        }
        total_r += -0.01F * static_cast<float>(a) + 0.1F * (sum / static_cast<float>(state_dim));
        for (std::size_t i = 0; i < state_dim; ++i) {
          state[i] += 0.05F * n01(rng);
        }
      }
      out_returns[g] = total_r;
      for (std::size_t i = 0; i < state_dim; ++i) {
        out_final_states[g * state_dim + i] = state[i];
      }
    });
  }
}
