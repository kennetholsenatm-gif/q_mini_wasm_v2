#include <cstdlib>

bool test_w158_pack();
bool test_ternary_matvec();
bool test_grammar_mask();
bool test_wles_hooks();
bool test_qaoa();

int main() {
  int fails = 0;
  if (!test_w158_pack()) {
    ++fails;
  }
  if (!test_ternary_matvec()) {
    ++fails;
  }
  if (!test_grammar_mask()) {
    ++fails;
  }
  if (!test_wles_hooks()) {
    ++fails;
  }
  if (!test_qaoa()) {
    ++fails;
  }
  return fails == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
