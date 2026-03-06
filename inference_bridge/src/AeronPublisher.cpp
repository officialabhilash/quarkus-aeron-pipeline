/**
 * Sends processed (translated) audio back via Aeron.
 * Takes output from TritonBridge callback, attaches the Request ID, and writes
 * into the local log buffer using Publication::offer(). Aeron asynchronously
 * routes the segment back to the Quarkus application.
 */
#include "AeronPublisher.h"
#include "MessageLayout.h"
#include <cstring>
#include <vector>
#include <thread>

#ifdef USE_AERON
#include <Aeron.h>
#include <Publication.h>
#include <concurrent/AtomicBuffer.h>
#endif

namespace bridge {

#ifdef USE_AERON
namespace {

constexpr size_t MAX_MESSAGE_SIZE = 64 * 1024;  // 64 KB per message; tune as needed
#endif
}

AeronPublisher::AeronPublisher(
    std::shared_ptr<aeron::Aeron> aeron,
    const std::string& channel,
    std::int32_t stream_id)
  : aeron_(std::move(aeron))
  , channel_(channel)
  , stream_id_(stream_id) {
#ifdef USE_AERON
  publication_id_ = aeron_->addPublication(channel_, stream_id_);
#else
  (void)publication_id_;
#endif
}

AeronPublisher::~AeronPublisher() = default;

void AeronPublisher::wait_until_ready() {
#ifdef USE_AERON
  while (!publication_) {
    publication_ = aeron_->findPublication(publication_id_);
    if (!publication_)
      std::this_thread::yield();
  }
#else
  (void)0;
#endif
}

bool AeronPublisher::offer(
    const uint8_t* request_id, size_t request_id_len,
    const uint8_t* payload, uint32_t payload_len) {
#ifdef USE_AERON
  if (!publication_ || !request_id || !payload) return false;
  if (request_id_len > MessageLayout::REQUEST_ID_SIZE) return false;
  size_t total = MessageLayout::HEADER_SIZE + payload_len;
  if (total > MAX_MESSAGE_SIZE) return false;

  std::vector<uint8_t> buffer(total);
  MessageLayout* layout = reinterpret_cast<MessageLayout*>(buffer.data());
  std::memset(layout->request_id, 0, MessageLayout::REQUEST_ID_SIZE);
  std::memcpy(layout->request_id, request_id, request_id_len);
  layout->payload_length = payload_len;
  std::memcpy(buffer.data() + MessageLayout::HEADER_SIZE, payload, payload_len);

  aeron::concurrent::AtomicBuffer srcBuffer(buffer.data(), static_cast<aeron::util::index_t>(total));
  std::int64_t result = publication_->offer(srcBuffer, 0, static_cast<aeron::util::index_t>(total));
  return result > 0;
#else
  (void)request_id;
  (void)request_id_len;
  (void)payload;
  (void)payload_len;
  return false;
#endif
}

}  // namespace bridge
