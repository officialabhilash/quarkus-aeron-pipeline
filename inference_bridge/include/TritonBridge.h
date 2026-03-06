#ifndef TRITON_BRIDGE_H
#define TRITON_BRIDGE_H

#include "MessageLayout.h"
#include <memory>
#include <string>
#include <functional>
#include <cstdint>
#include <cstddef>

// Triton C API types (opaque pointers in C API)
struct TRITONSERVER_Server;
struct TRITONSERVER_InferenceRequest;
struct TRITONSERVER_InferenceResponse;

namespace bridge {

class AeronPublisher;

/**
 * Manages the Triton C API lifecycle and inference requests.
 * Wraps audio data into TRITONSERVER_InferenceRequest and submits via
 * TRITONSERVER_ServerInferAsync. Defines the completion callback that Triton
 * invokes when the model finishes; the callback passes output tensors to
 * AeronPublisher for sending back over Aeron.
 */
class TritonBridge {
public:
  /** Callback when inference completes: (request_id, request_id_len, output_audio, output_len). */
  using InferenceCompleteCallback = std::function<void(
    const uint8_t* request_id, size_t request_id_len,
    const void* output_audio, uint32_t output_len)>;

  /**
   * \param model_name Triton model name (e.g. "asr" or ensemble).
   * \param on_complete Called when inference completes (e.g. to publish via Aeron).
   */
  TritonBridge(const std::string& model_name, InferenceCompleteCallback on_complete);

  ~TritonBridge();

  /** Initialize Triton server (TRITONSERVER_ServerNew). Call once before request_infer. */
  bool init(const std::string& model_repository_path = "",
            const std::string& config_path = "");

  /** Shutdown and destroy server. */
  void shutdown();

  /**
   * Submit an inference request for one audio chunk.
   * \param request_id Session/request ID to attach to the response.
   * \param request_id_len Length of request_id.
   * \param audio_input Raw audio bytes (input tensor data).
   * \param audio_len Length of audio_input.
   * \return true if request was submitted successfully.
   */
  bool request_infer(
    const uint8_t* request_id, size_t request_id_len,
    const void* audio_input, uint32_t audio_len);

  TRITONSERVER_Server* server() const { return server_; }

private:
  std::string model_name_;
  InferenceCompleteCallback on_complete_;
  TRITONSERVER_Server* server_{nullptr};
};

}  // namespace bridge

#endif  // TRITON_BRIDGE_H
