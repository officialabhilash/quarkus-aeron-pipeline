#ifndef MESSAGE_LAYOUT_H
#define MESSAGE_LAYOUT_H

#include <cstdint>
#include <cstddef>

namespace bridge {

/**
 * Fixed layout for the binary payload exchanged over Aeron.
 * Used to interpret incoming audio chunks and to build outbound translated audio.
 * Layout is kept minimal for low parsing overhead (SBE-style encoding can be layered later).
 */
struct MessageLayout {
  /** Application-level request/session identifier; used to route responses back to the correct WebSocket. */
  static constexpr size_t REQUEST_ID_SIZE = 16;  // e.g. UUID 128-bit or custom 16-byte ID
  static constexpr size_t HEADER_SIZE = REQUEST_ID_SIZE + sizeof(uint32_t);  // request_id + payload_length

  /** Request ID bytes (session/correlation). */
  uint8_t request_id[REQUEST_ID_SIZE];
  /** Length of the following raw audio (or translated audio) bytes. */
  uint32_t payload_length;
  /** Payload follows immediately in memory (raw audio or synthesized audio). */
  // uint8_t payload[payload_length];
};

/**
 * View over a received buffer: request_id + length, then payload pointer.
 */
struct IncomingMessage {
  const uint8_t* request_id;
  uint32_t payload_length;
  const uint8_t* payload;
};

/**
 * Helper to overlay MessageLayout at the start of a buffer and get payload pointer.
 */
inline IncomingMessage view_incoming(const uint8_t* buffer, size_t buffer_len) {
  IncomingMessage msg{};
  if (buffer_len < MessageLayout::HEADER_SIZE) return msg;
  const auto* layout = reinterpret_cast<const MessageLayout*>(buffer);
  msg.request_id = layout->request_id;
  msg.payload_length = layout->payload_length;
  if (buffer_len >= MessageLayout::HEADER_SIZE + layout->payload_length)
    msg.payload = buffer + MessageLayout::HEADER_SIZE;
  else
    msg.payload = nullptr;
  return msg;
}

}  // namespace bridge

#endif  // MESSAGE_LAYOUT_H
