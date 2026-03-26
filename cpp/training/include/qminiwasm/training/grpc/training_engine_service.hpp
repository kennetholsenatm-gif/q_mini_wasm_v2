#pragma once

#include <grpcpp/server_builder.h>

namespace qminiwasm::training::grpcsvc {

void RegisterTrainingEngineService(::grpc::ServerBuilder& builder);

}  // namespace qminiwasm::training::grpcsvc
