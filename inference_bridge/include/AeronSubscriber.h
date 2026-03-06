#ifndef AERON_SUBSCRIBER_H
#define AERON_SUBSCRIBER_H

#include "MessageLayout.h"
#include <memory>
#include <string>
#include <atomic>
#include <functional>
#include <thread>
#include <cstdint>

// Forward declarations for Aeron types (include actual headers in .cpp)
namespace aeron {
class Aeron;
class Subscription;
}
namespace concurrent {
template<typename T> class AtomicBuffer;
}
namespace util {
typedef std::ptrdiff_t index_t;
}
struct Header;

namespace bridge {

/**
 * Handles Aeron subscription and polling.
 * Runs a dedicated thread that continuously polls the subscription for new fragments.
 * When a message arrives, the FragmentHandler is invoked with the decoded request ID and raw audio bytes.
 */
class AeronSubscriber {
public:
  /** Callback: (request_id, request_id_len, payload, payload_len). Called from poll thread. */
  using FragmentHandler = std::function<void(
    const uint8_t* request_id, size_t request_id_len,
    const uint8_t* payload, uint32_t payload_len)>;

  /**
   * \param aeron Connected Aeron instance (shared with other components).
   * \param channel Channel URI (e.g. "aeron:ipc" for IPC).
   * \param stream_id Stream ID for inbound raw audio.
   * \param on_fragment Callback invoked for each received message.
   */
  AeronSubscriber(
    std::shared_ptr<aeron::Aeron> aeron,
    const std::string& channel,
    std::int32_t stream_id,
    FragmentHandler on_fragment);

  ~AeronSubscriber();

  /** Start the polling thread. */
  void start();
  /** Signal stop and join the polling thread. */
  void stop();

  /** Block until the subscription is available (after addSubscription). */
  void wait_until_ready();

private:
  void poll_loop();

  std::shared_ptr<aeron::Aeron> aeron_;
  std::string channel_;
  std::int32_t stream_id_;
  FragmentHandler on_fragment_;
  std::int64_t subscription_id_{0};
  std::shared_ptr<aeron::Subscription> subscription_;
  std::atomic<bool> running_{false};
  std::unique_ptr<std::thread> thread_;
};

}  // namespace bridge

#endif  // AERON_SUBSCRIBER_H
