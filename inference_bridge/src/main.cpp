/**
 * Application entry point: initialize Aeron context, Triton server, and start the event loop.
 * Wires AeronSubscriber -> TritonBridge -> AeronPublisher.
 */
#include "AeronSubscriber.h"
#include "AeronPublisher.h"
#include "TritonBridge.h"
#include "MessageLayout.h"
#include <iostream>
#include <csignal>
#include <memory>
#include <chrono>
#include <thread>

#ifdef USE_AERON
#include <Aeron.h>
#endif

#ifdef USE_TRITON
#include "triton/core/tritonserver.h"
#endif

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int) {
  g_running = false;
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  const std::string aeron_channel = "aeron:ipc";
  const std::int32_t inbound_stream_id = 100;   // raw audio from Quarkus
  const std::int32_t outbound_stream_id = 200;  // translated audio to Quarkus
  const std::string triton_model = "asr";       // or ensemble name

#ifdef USE_AERON
  aeron::Context context;
  context.aeronDir(std::getenv("AERON_DIR") ? std::getenv("AERON_DIR") : "/dev/shm/aeron");
  std::shared_ptr<aeron::Aeron> aeron = aeron::Aeron::connect(context);

  // Publisher for translated audio (used by Triton callback)
  auto publisher = std::make_shared<bridge::AeronPublisher>(aeron, aeron_channel, outbound_stream_id);
  publisher->wait_until_ready();

  // Triton bridge: on completion, publish back via Aeron
  auto triton_bridge = std::make_shared<bridge::TritonBridge>(triton_model,
    [publisher](const uint8_t* request_id, size_t request_id_len,
                const void* output_audio, uint32_t output_len) {
      if (output_audio && output_len > 0)
        publisher->offer(request_id, request_id_len,
                         static_cast<const uint8_t*>(output_audio), output_len);
    });

#ifdef USE_TRITON
  if (!triton_bridge->init()) {
    std::cerr << "Triton init failed\n";
    return 1;
  }
#endif

  // Subscriber: on fragment -> TritonBridge::request_infer
  auto subscriber = std::make_unique<bridge::AeronSubscriber>(aeron, aeron_channel, inbound_stream_id,
    [triton_bridge](const uint8_t* request_id, size_t request_id_len,
                   const uint8_t* payload, uint32_t payload_len) {
      if (payload && payload_len > 0)
        triton_bridge->request_infer(request_id, request_id_len, payload, payload_len);
    });

  subscriber->wait_until_ready();
  subscriber->start();

  while (g_running)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

  subscriber->stop();
#ifdef USE_TRITON
  triton_bridge->shutdown();
#endif
#else
  std::cerr << "Build with USE_AERON and USE_TRITON (AERON_ROOT and TRITON_ROOT set)\n";
  return 1;
#endif

  return 0;
}
