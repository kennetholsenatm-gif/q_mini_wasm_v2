#include <grpcpp/grpcpp.h>

#include <iostream>
#include <memory>
#include <string>

#include "qminiwasm/training/grpc/training_engine_service.hpp"

int main(int argc, char** argv) {
  std::string listen_addr = "127.0.0.1:50061";
  if (argc > 1) {
    listen_addr = argv[1];
  }

  grpc::ServerBuilder builder;
  builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
  qminiwasm::training::grpcsvc::RegisterTrainingEngineService(builder);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  if (!server) {
    std::cerr << "failed to start training_engine_server\n";
    return 1;
  }
  std::cout << "training_engine_server listening on " << listen_addr << "\n";
  server->Wait();
  return 0;
}
