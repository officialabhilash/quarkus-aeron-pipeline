#ifndef AERON_PUBLISHER_H
#define AERON_PUBLISHER_H

#include "MessageLayout.h"
#include <memory>
#include <string>
#include <cstdint>
#include <cstddef>

namespace aeron {
class Aeron;
class Publication;
}
namespace concurrent {
template<typename T> class AtomicBuffer;
}

namespace bridge {

/**
 * Handles sending processed (translated) audio back via Aeron.
 * Takes the output from Triton, attaches the original Request ID for session state,
 * and writes the bytes using Publication::offer() into the local log buffer.
 * Aeron asynchronously routes the segment back to the Quarkus application.
 */
class AeronPublisher {
public:
  /**
   * \param aeron Connected Aeron instance.
   * \param channel Channel URI (e.g. "aeron:ipc").
   * \param stream_id Stream ID for outbound translated audio.
   */
  AeronPublisher(
    std::shared_ptr<aeron::Aeron> aeron,
    const std::string& channel,
    std::int32_t stream_id);

  ~AeronPublisher();

  /**
   * Publish translated audio with the given request ID.
   * \param request_id Application request/session ID (e.g. 16 bytes).
   * \param request_id_len Length of request_id (must be <= MessageLayout::REQUEST_ID_SIZE).
   * \param payload Translated audio bytes.
   * \param payload_len Length of payload.
   * \return true if offer succeeded (result > 0); false on back pressure or error.
   */
  bool offer(
    const uint8_t* request_id, size_t request_id_len,
    const uint8_t* payload, uint32_t payload_len);

  /** Block until the publication is available. */
  void wait_until_ready();

private:
  std::shared_ptr<aeron::Aeron> aeron_;
  std::string channel_;
  std::int32_t stream_id_;
  std::int64_t publication_id_{0};
  std::shared_ptr<aeron::Publication> publication_;
};

}  // namespace bridge

#endif  // AERON_PUBLISHER_H
