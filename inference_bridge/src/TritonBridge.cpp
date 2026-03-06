/**
 * Manages Triton In-Process C API: TRITONSERVER_ServerNew, TRITONSERVER_ServerInferAsync.
 * Wraps audio into TRITONSERVER_InferenceRequest; completion callback passes output
 * tensors to the configured callback (e.g. AeronPublisher).
 */
#include "TritonBridge.h"
#include <cstring>
#include <vector>
#include <iostream>

#ifdef USE_TRITON
#include "triton/core/tritonserver.h"
#endif

namespace bridge {

#ifdef USE_TRITON
namespace {

struct RequestContext {
  uint8_t request_id[MessageLayout::REQUEST_ID_SIZE];
  size_t request_id_len;
  TritonBridge::InferenceCompleteCallback on_complete;
};

void response_complete_callback(
    TRITONSERVER_InferenceResponse* response,
    uint32_t flags,
    void* userp) {
  (void)flags;
  RequestContext* ctx = static_cast<RequestContext*>(userp);
  if (!ctx || !response) return;

  TRITONSERVER_Error* err = TRITONSERVER_InferenceResponseError(response);
  if (err) {
    std::cerr << "Triton response error\n";
    TRITONSERVER_ErrorDelete(err);
    delete ctx;
    return;
  }

  // Simplified: read first output tensor as audio (name/model-specific)
  const char* name;
  TRITONSERVER_DataType dtype;
  const int64_t* shape;
  uint64_t dim_count;
  const void* base;
  size_t byte_size;
  TRITONSERVER_MemoryType memory_type;
  int64_t memory_type_id;

  uint32_t output_count;
  err = TRITONSERVER_InferenceResponseOutputCount(response, &output_count);
  if (err || output_count == 0) {
    if (err) TRITONSERVER_ErrorDelete(err);
    ctx->on_complete(ctx->request_id, ctx->request_id_len, nullptr, 0);
    delete ctx;
    return;
  }

  err = TRITONSERVER_InferenceResponseOutput(response, 0, &name, &dtype, &shape, &dim_count, &base, &byte_size, &memory_type, &memory_type_id);
  if (err) {
    TRITONSERVER_ErrorDelete(err);
    ctx->on_complete(ctx->request_id, ctx->request_id_len, nullptr, 0);
    delete ctx;
    return;
  }

  ctx->on_complete(ctx->request_id, ctx->request_id_len, base, static_cast<uint32_t>(byte_size));
  delete ctx;
}

}  // namespace
#endif

TritonBridge::TritonBridge(const std::string& model_name, InferenceCompleteCallback on_complete)
  : model_name_(model_name)
  , on_complete_(std::move(on_complete)) {}

TritonBridge::~TritonBridge() {
  shutdown();
}

bool TritonBridge::init(const std::string& model_repository_path, const std::string& config_path) {
#ifdef USE_TRITON
  TRITONSERVER_ServerOptions* opts = nullptr;
  TRITONSERVER_Error* err = TRITONSERVER_ServerOptionsNew(&opts);
  if (err) {
    TRITONSERVER_ErrorDelete(err);
    return false;
  }
  if (!model_repository_path.empty())
    err = TRITONSERVER_ServerOptionsSetModelRepositoryPath(opts, model_repository_path.c_str());
  if (!err && !config_path.empty())
    err = TRITONSERVER_ServerOptionsSetConfigPath(opts, config_path.c_str());
  if (err) {
    TRITONSERVER_ErrorDelete(err);
    TRITONSERVER_ServerOptionsDelete(opts);
    return false;
  }
  err = TRITONSERVER_ServerNew(&server_, opts);
  TRITONSERVER_ServerOptionsDelete(opts);
  if (err) {
    TRITONSERVER_ErrorDelete(err);
    return false;
  }
  err = TRITONSERVER_ServerWaitUntilReady(server_);
  if (err) {
    TRITONSERVER_ErrorDelete(err);
    TRITONSERVER_ServerDelete(server_);
    server_ = nullptr;
    return false;
  }
  return true;
#else
  (void)model_repository_path;
  (void)config_path;
  return false;
#endif
}

void TritonBridge::shutdown() {
#ifdef USE_TRITON
  if (server_) {
    TRITONSERVER_ServerStop(server_);
    TRITONSERVER_ServerDelete(server_);
    server_ = nullptr;
  }
#else
  (void)0;
#endif
}

bool TritonBridge::request_infer(
    const uint8_t* request_id, size_t request_id_len,
    const void* audio_input, uint32_t audio_len) {
#ifdef USE_TRITON
  if (!server_ || !request_id || !audio_input) return false;
  if (request_id_len > MessageLayout::REQUEST_ID_SIZE) return false;

  TRITONSERVER_InferenceRequest* irequest = nullptr;
  TRITONSERVER_Error* err = TRITONSERVER_InferenceRequestNew(&irequest, server_, model_name_.c_str(), -1);
  if (err) {
    TRITONSERVER_ErrorDelete(err);
    return false;
  }

  // Input: single audio input (name and shape are model-specific; adjust per model config)
  err = TRITONSERVER_InferenceRequestSetId(irequest, reinterpret_cast<const char*>(request_id));
  if (err) {
    TRITONSERVER_ErrorDelete(err);
    TRITONSERVER_InferenceRequestDelete(irequest);
    return false;
  }
  err = TRITONSERVER_InferenceRequestAddInput(irequest, "AUDIO", TRITONSERVER_TYPE_INT16, nullptr, 0);
  if (err) {
    TRITONSERVER_ErrorDelete(err);
    TRITONSERVER_InferenceRequestDelete(irequest);
    return false;
  }
  err = TRITONSERVER_InferenceRequestAppendInputData(irequest, "AUDIO", audio_input, audio_len, TRITONSERVER_MEMORY_CPU, 0);
  if (err) {
    TRITONSERVER_ErrorDelete(err);
    TRITONSERVER_InferenceRequestDelete(irequest);
    return false;
  }

  auto* ctx = new RequestContext;
  std::memset(ctx->request_id, 0, MessageLayout::REQUEST_ID_SIZE);
  std::memcpy(ctx->request_id, request_id, request_id_len);
  ctx->request_id_len = request_id_len;
  ctx->on_complete = on_complete_;

  err = TRITONSERVER_InferenceRequestSetResponseCallback(irequest, nullptr, 0, response_complete_callback, ctx);
  if (err) {
    TRITONSERVER_ErrorDelete(err);
    delete ctx;
    TRITONSERVER_InferenceRequestDelete(irequest);
    return false;
  }

  err = TRITONSERVER_ServerInferAsync(server_, irequest, nullptr);
  TRITONSERVER_InferenceRequestDelete(irequest);
  if (err) {
    TRITONSERVER_ErrorDelete(err);
    delete ctx;
    return false;
  }
  return true;
#else
  (void)request_id;
  (void)request_id_len;
  (void)audio_input;
  (void)audio_len;
  return false;
#endif
}

}  // namespace bridge
