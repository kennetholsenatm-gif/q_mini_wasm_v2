#include "ota_state_machine.hpp"

#include <future>

namespace qminiwasm::agent {

OtaStateMachine::OtaStateMachine(ObserveFn o, ThinkFn t, ActFn a, ToolResultFn tr)
    : observe_(std::move(o)), think_(std::move(t)), act_(std::move(a)), tool_result_(std::move(tr)) {}

void OtaStateMachine::reset() {
  state_ = OtaState::Observe;
  step_budget_ = 0;
  pending_tool_ = {};
}

std::expected<std::monostate, ExecutionError> OtaStateMachine::step() {
  if (step_budget_++ >= kMaxSteps) {
    return std::unexpected(ExecutionError{ExecutionErrorCode::BudgetExceeded, "step budget exceeded", {}});
  }
  switch (state_) {
    case OtaState::Observe: {
      auto r = observe_();
      if (!r) {
        return std::unexpected(r.error());
      }
      last_obs_ = *r;
      state_ = OtaState::Think;
      return {};
    }
    case OtaState::Think: {
      auto r = think_(last_obs_);
      if (!r) {
        return std::unexpected(r.error());
      }
      last_thought_ = *r;
      state_ = OtaState::Act;
      return {};
    }
    case OtaState::Act: {
      auto r = act_(last_thought_);
      if (!r) {
        const ExecutionError& e = r.error();
        if (e.code == ExecutionErrorCode::ToolIntercept) {
          pending_tool_ = e.tool;
          state_ = OtaState::AwaitTool;
          return {};
        }
        return std::unexpected(e);
      }
      state_ = OtaState::Done;
      return {};
    }
    case OtaState::AwaitTool: {
      auto r = tool_result_(pending_tool_);
      if (!r) {
        return std::unexpected(r.error());
      }
      last_obs_ = *r;
      state_ = OtaState::Think;
      pending_tool_ = {};
      return {};
    }
    case OtaState::Done:
      return std::unexpected(ExecutionError{ExecutionErrorCode::InvalidState, "already done", {}});
  }
  return std::unexpected(ExecutionError{ExecutionErrorCode::InvalidState, "unknown state", {}});
}

std::future<std::expected<std::monostate, ExecutionError>> OtaStateMachine::step_async() {
  return std::async(std::launch::async, [this]() { return this->step(); });
}

}  // namespace qminiwasm::agent
