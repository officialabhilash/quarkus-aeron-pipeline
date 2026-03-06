/**
 * Manages the incoming Aeron data stream: dedicated thread running a continuous
 * polling loop (Subscription::poll). FragmentHandler is invoked when a message arrives;
 * we decode Request ID and raw audio bytes from the payload and pass them to the callback.
 */
#include "AeronSubscriber.h"
#include "MessageLayout.h"
#include <thread>
#include <iostream>

#ifdef USE_AERON
#include <Aeron.h>
#include <concurrent/AtomicBuffer.h>
#include <Subscription.h>
#include <util/IdleStrategy.h>
#include <util/BitUtil.h>
#endif

namespace bridge {

#ifdef USE_AERON
namespace {

using namespace aeron;
using namespace aeron::util;

const int FRAGMENT_LIMIT = 10;
const int IDLE_SLEEP_MS = 1;

aeron::fragment_handler_t make_handler(AeronSubscriber::FragmentHandler on_fragment) {
  return [on_fragment](const concurrent::AtomicBuffer& buffer, index_t offset, index_t length, const Header& header) {
    (void)header;
    if (length < static_cast<index_t>(MessageLayout::HEADER_SIZE)) return;
    IncomingMessage msg = view_incoming(buffer.buffer() + offset, static_cast<size_t>(length));
    if (!msg.payload) return;
    on_fragment(msg.request_id, MessageLayout::REQUEST_ID_SIZE,
                msg.payload, msg.payload_length);
  };
}

}  // namespace
#endif

AeronSubscriber::AeronSubscriber(
    std::shared_ptr<aeron::Aeron> aeron,
    const std::string& channel,
    std::int32_t stream_id,
    FragmentHandler on_fragment)
  : aeron_(std::move(aeron))
  , channel_(channel)
  , stream_id_(stream_id)
  , on_fragment_(std::move(on_fragment)) {
#ifdef USE_AERON
  subscription_id_ = aeron_->addSubscription(channel_, stream_id_);
#else
  (void)subscription_id_;
#endif
}

AeronSubscriber::~AeronSubscriber() {
  stop();
}

void AeronSubscriber::wait_until_ready() {
#ifdef USE_AERON
  while (!subscription_) {
    subscription_ = aeron_->findSubscription(subscription_id_);
    if (!subscription_)
      std::this_thread::yield();
  }
#else
  (void)0;
#endif
}

void AeronSubscriber::start() {
  if (running_.exchange(true)) return;
#ifdef USE_AERON
  thread_ = std::make_unique<std::thread>(&AeronSubscriber::poll_loop, this);
#else
  (void)0;
#endif
}

void AeronSubscriber::stop() {
  running_ = false;
  if (thread_ && thread_->joinable())
    thread_->join();
  thread_.reset();
}

void AeronSubscriber::poll_loop() {
#ifdef USE_AERON
  aeron::SleepingIdleStrategy idle(IDLE_SLEEP_MS);
  auto handler = make_handler(on_fragment_);
  while (running_ && subscription_) {
    int fragments_read = subscription_->poll(handler, FRAGMENT_LIMIT);
    idle.idle(fragments_read);
  }
#else
  while (running_) std::this_thread::yield();
#endif
}

}  // namespace bridge
