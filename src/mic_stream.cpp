/**
 * @file src/mic_stream.cpp
 * @brief Definitions for receiving a client's microphone audio.
 */
// standard includes
#include <array>
#include <chrono>
#include <cstring>
#include <list>
#include <mutex>
#include <vector>

// lib includes
#include <opus/opus.h>

// local includes
#include "logging.h"
#include "mic_stream.h"
#include "platform/common.h"
#include "utility.h"

using namespace std::literals;

namespace mic_stream {
  namespace {
    constexpr std::array<char, 4> MAGIC {'M', 'I', 'C', 0x01};
    constexpr int SAMPLE_RATE = 48000;
    constexpr int CHANNELS = 1;
    constexpr int MAX_FRAME = SAMPLE_RATE * 120 / 1000;  // Largest Opus frame
    constexpr int MAX_CONCEALED_FRAMES = 3;

    struct client_t {
      boost::asio::ip::address address;
      crypto::cipher::gcm_t cipher;
      crypto::aes_t iv;

      bool started = false;
      std::uint32_t next_seq = 0;
      int last_frame_size = SAMPLE_RATE / 50;

      util::safe_ptr<OpusDecoder, opus_decoder_destroy> decoder;
      std::unique_ptr<platf::virtual_mic_t> mic;
      bool mic_unavailable = false;

      std::uint64_t packets = 0;
      std::uint64_t lost = 0;
    };

    std::mutex clients_mutex;
    std::list<std::shared_ptr<client_t>> clients;

    boost::asio::ip::address normalize(const boost::asio::ip::address &address) {
      if (address.is_v6() && address.to_v6().is_v4_mapped()) {
        return boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, address.to_v6());
      }
      return address;
    }

    void play(client_t &client, const unsigned char *opus, int opus_len) {
      if (!client.decoder) {
        int err = 0;
        client.decoder.reset(opus_decoder_create(SAMPLE_RATE, CHANNELS, &err));
        if (!client.decoder) {
          BOOST_LOG(error) << "Client mic: couldn't create Opus decoder: "sv << err;
          client.mic_unavailable = true;
          return;
        }
      }

      if (!client.mic && !client.mic_unavailable) {
        BOOST_LOG(info) << "Client mic: receiving microphone audio from "sv << client.address.to_string();
        client.mic = platf::virtual_mic(SAMPLE_RATE, CHANNELS);
        if (!client.mic) {
          BOOST_LOG(warning) << "Client mic: this host can't play client microphone audio"sv;
          client.mic_unavailable = true;
        }
      }

      if (!client.mic) {
        return;
      }

      std::array<opus_int16, MAX_FRAME * CHANNELS> pcm;
      auto frames = opus_decode(client.decoder.get(), opus, opus_len, pcm.data(), MAX_FRAME, 0);
      if (frames > 0) {
        client.last_frame_size = frames;
        client.mic->write(pcm.data(), frames);
      }
    }

    void conceal(client_t &client, int count) {
      if (!client.decoder || !client.mic) {
        return;
      }
      std::array<opus_int16, MAX_FRAME * CHANNELS> pcm;
      for (int x = 0; x < count; ++x) {
        auto frames = opus_decode(client.decoder.get(), nullptr, 0, pcm.data(), client.last_frame_size, 0);
        if (frames > 0) {
          client.mic->write(pcm.data(), frames);
        }
      }
    }
  }  // namespace

  bool is_mic_packet(std::string_view data) {
    return data.size() > HEADER_SIZE && std::memcmp(data.data(), MAGIC.data(), MAGIC.size()) == 0;
  }

  std::shared_ptr<void> register_client(const boost::asio::ip::address &address, const crypto::aes_t &key) {
    auto client = std::make_shared<client_t>();
    client->address = normalize(address);
    client->cipher = crypto::cipher::gcm_t {key, false};

    {
      std::lock_guard lg {clients_mutex};
      clients.push_back(client);
    }

    // The returned handle unregisters the client when the session's audio stream ends
    return std::shared_ptr<void>(nullptr, [client](void *) {
      std::unique_ptr<platf::virtual_mic_t> mic;
      {
        std::lock_guard lg {clients_mutex};
        clients.remove(client);
        mic = std::move(client->mic);
        if (client->packets) {
          BOOST_LOG(info) << "Client mic: stream ended ("sv << client->packets << " packets, "sv << client->lost << " lost)"sv;
        }
      }
      // Stop playback (and restore the default recording device) outside the lock
      mic.reset();
    });
  }

  void handle_packet(const boost::asio::ip::address &address, std::string_view data) {
    if (!is_mic_packet(data)) {
      return;
    }

    std::uint32_t seq;
    std::memcpy(&seq, data.data() + 4, sizeof(seq));
    seq = util::endian::little(seq);

    auto tagged_cipher = data.substr(8);
    auto peer = normalize(address);

    std::lock_guard lg {clients_mutex};

    // Try the session from this address first. The key is what authenticates the packet,
    // so fall back to the others too (the client's mic socket can pick a different
    // source address than its audio socket, e.g. an IPv6 temporary address).
    std::vector<std::shared_ptr<client_t>> candidates;
    for (auto &client : clients) {
      if (client->address == peer) {
        candidates.insert(std::begin(candidates), client);
      } else {
        candidates.push_back(client);
      }
    }

    for (auto &client : candidates) {

      client->iv.assign(12, 0);
      std::memcpy(client->iv.data(), &data[4], 4);  // already little endian on the wire
      client->iv[10] = 'C';  // Client originated
      client->iv[11] = 'M';  // Microphone stream

      std::vector<std::uint8_t> plaintext;
      if (client->cipher.decrypt(tagged_cipher, plaintext, &client->iv)) {
        // Not this session's key (several clients can share an address behind NAT)
        continue;
      }

      if (client->started) {
        auto delta = (std::int32_t) (seq - client->next_seq);
        if (delta < 0) {
          // Late or replayed packet
          return;
        }
        if (delta > 0) {
          client->lost += delta;
          conceal(*client, std::min(delta, MAX_CONCEALED_FRAMES));
        }
      }

      client->started = true;
      client->next_seq = seq + 1;
      ++client->packets;

      if (!plaintext.empty()) {
        play(*client, plaintext.data(), (int) plaintext.size());
      }
      return;
    }
  }
}  // namespace mic_stream
