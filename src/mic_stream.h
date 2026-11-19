/**
 * @file src/mic_stream.h
 * @brief Declarations for receiving a client's microphone audio.
 *
 * Wire format (client -> host, UDP, sent to the session's audio port):
 *   0..3   'M' 'I' 'C' 0x01   magic + version
 *   4..7   sequence number, little endian, +1 per packet
 *   8..23  AES-GCM tag
 *   24..   AES-128-GCM ciphertext of one Opus frame (48 kHz, mono)
 * The key is the session's remote input key (rikey). The 12-byte IV is the
 * sequence number (little endian) in bytes 0..3, zeros, then 'C' 'M' in bytes 10..11.
 */
#pragma once

// standard includes
#include <memory>
#include <string_view>

// lib includes
#include <boost/asio/ip/address.hpp>

// local includes
#include "crypto.h"

namespace mic_stream {
  constexpr std::size_t HEADER_SIZE = 4 + 4 + 16;

  /**
   * @brief Whether a datagram received on the audio port is client microphone audio.
   */
  bool is_mic_packet(std::string_view data);

  /**
   * @brief Accept microphone audio for a session from `address` encrypted with `key`.
   * @return Handle that stops accepting (and closes the virtual mic) when destroyed.
   */
  std::shared_ptr<void> register_client(const boost::asio::ip::address &address, const crypto::aes_t &key);

  /**
   * @brief Decrypt, decode and play a microphone packet. Called from the network thread.
   */
  void handle_packet(const boost::asio::ip::address &address, std::string_view data);
}  // namespace mic_stream
