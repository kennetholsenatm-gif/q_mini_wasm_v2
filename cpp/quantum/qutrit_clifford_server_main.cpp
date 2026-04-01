#include "qutrit_clifford_service.hpp"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  std::string address = "0.0.0.0:50052";

  if (argc > 1) {
    address = argv[1];
  }

  std::cout << "Starting QutritCliffordService on " << address << std::endl;

  auto service = std::make_shared<qminiwasm::quantum::QutritCliffordServiceImpl>();
  qminiwasm::quantum::RunQutritCliffordServer(address, service);

  return 0;
}