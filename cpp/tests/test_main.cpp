#include <cstdlib>

bool test_escalation_policy();
bool test_expert_fleet();
bool test_w158_pack();
bool test_ternary_matvec();
bool test_grammar_mask();
bool test_linear_memory_encode();
bool test_wles_hooks();
bool test_wasm_hooks();
bool test_qaoa();
#if QMINIWASM_HAS_QUANTUM
bool test_openqasm_quantum();
#endif

int main() {
  int fails = 0;
  if (!test_escalation_policy()) {
    ++fails;
  }
  if (!test_expert_fleet()) {
    ++fails;
  }
  if (!test_w158_pack()) {
    ++fails;
  }
  if (!test_ternary_matvec()) {
    ++fails;
  }
  if (!test_grammar_mask()) {
    ++fails;
  }
  if (!test_linear_memory_encode()) {
    ++fails;
  }
  if (!test_wles_hooks()) {
    ++fails;
  }
  if (!test_wasm_hooks()) {
    ++fails;
  }
  if (!test_qaoa()) {
    ++fails;
  }
#if QMINIWASM_HAS_QUANTUM
  if (!test_openqasm_quantum()) {
    ++fails;
  }
#endif
  return fails == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
