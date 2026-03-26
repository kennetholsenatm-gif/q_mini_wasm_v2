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

  grpc::Status ApplyGraph(grpc::ServerContext* /*context*/,
                          const qminiwasm::delta::GraphTopologyUpdate* request,
                          qminiwasm::delta::GraphApplyAck* response) override {
    if (request == nullptr || request->graph_id().empty() || request->node_id().empty()) {
      response->set_accepted(false);
      response->set_status("invalid_argument");
      response->set_graph_id(request != nullptr ? request->graph_id() : "");
      response->set_node_id(request != nullptr ? request->node_id() : "");
      response->set_message("graph_id and node_id are required");
      return grpc::Status::OK;
    }
    response->set_accepted(true);
    response->set_status("accepted");
    response->set_graph_id(request->graph_id());
    response->set_node_id(request->node_id());
    response->set_message("graph update accepted");
    return grpc::Status::OK;
  }
};

/** Minimal skeleton: register service on supplied builder (caller runs server). */
void RegisterDeltaSyncService(grpc::ServerBuilder& builder) {
  static DeltaSyncService service;
  builder.RegisterService(&service);
}

}  // namespace qminiwasm::grpcsvc
