#include <grpcpp/grpcpp.h>

#include "delta_sync.grpc.pb.h"

#include <string>

namespace qminiwasm::grpcsvc {

class DeltaSyncService final : public qminiwasm::delta::DeltaSync::Service {
 public:
  grpc::Status PushDelta(grpc::ServerContext* /*context*/, const qminiwasm::delta::DeltaTensor* /*request*/,
                         qminiwasm::delta::DeltaAck* response) override {
    response->set_applied_version(1);
    response->set_status("ok");
    return grpc::Status::OK;
  }

  grpc::Status StreamDeltas(grpc::ServerContext* /*context*/,
                              grpc::ServerReaderWriter<qminiwasm::delta::DeltaTensor,
                                                       qminiwasm::delta::DeltaTensor>* /*stream*/) override {
    return grpc::Status::OK;
  }
};

/** Minimal skeleton: register service on supplied builder (caller runs server). */
void RegisterDeltaSyncService(grpc::ServerBuilder& builder) {
  static DeltaSyncService service;
  builder.RegisterService(&service);
}

}  // namespace qminiwasm::grpcsvc
