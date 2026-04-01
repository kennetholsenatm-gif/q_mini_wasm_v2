#include "qutrit_training_service.hpp"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  std::string address = "0.0.0.0:50053";

  if (argc > 1) {
    address = argv[1];
  }

  std::cout << "Starting QutritTrainingService on " << address << std::endl;

  auto service = std::make_shared<qminiwasm::quantum::QutritTrainingServiceImpl>();
  qminiwasm::quantum::RunQutritTrainingServer(address, service);

  return 0;
}