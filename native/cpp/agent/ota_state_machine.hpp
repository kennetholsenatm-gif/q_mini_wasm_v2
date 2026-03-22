#pragma once

#include <expected>
#include <functional>
#include <future>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qminiwasm::agent {

struct Observation {
  std::string text;
  int token_count = 0;
};

struct Thought {
  std::string rationale;
};

struct ActionSample {
  std::string text;
};

struct ToolCallIntercepted {
  std::string tool_name;
  std::vector<std::uint8_t> args_blob;
};

enum class ExecutionErrorCode {
  ToolIntercept,
  InvalidState,
  BudgetExceeded,
};

struct ExecutionError {
  ExecutionErrorCode code = ExecutionErrorCode::InvalidState;
  std::string message;
  ToolCallIntercepted tool;
};

enum class OtaState {
  Observe,
  Think,
  Act,
  AwaitTool,
  Done,
};

/**
 * Embedded Observation–Thought–Action cycle without exceptions on the hot path.
 * Returns std::expected for transitions; tool interception uses ExecutionError.
 */
class OtaStateMachine {
 public:
  using ObserveFn = std::function<std::expected<Observation, ExecutionError>()>;
  using ThinkFn = std::function<std::expected<Thought, ExecutionError>(const Observation&)>;
  using ActFn = std::function<std::expected<ActionSample, ExecutionError>(const Thought&)>;
  using ToolResultFn = std::function<std::expected<Observation, ExecutionError>(ToolCallIntercepted)>;

  OtaStateMachine(ObserveFn o, ThinkFn t, ActFn a, ToolResultFn tr);

  /** Single synchronous step; advances internal state. */
  std::expected<std::monostate, ExecutionError> step();

  /** Async wrapper (std::async); same semantics as step(). */
  std::future<std::expected<std::monostate, ExecutionError>> step_async();

  OtaState state() const { return state_; }
  bool finished() const { return state_ == OtaState::Done; }

  void reset();

 private:
  ObserveFn observe_;
  ThinkFn think_;
  ActFn act_;
  ToolResultFn tool_result_;
  OtaState state_ = OtaState::Observe;
  Observation last_obs_;
  Thought last_thought_;
  ToolCallIntercepted pending_tool_{};
  int step_budget_ = 0;
  static constexpr int kMaxSteps = 4096;
};

}  // namespace qminiwasm::agent
