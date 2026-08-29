#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <ranges>
#include <vector>

#include "price_update_service.hpp"

struct SentMessage {
  StationId station;
  UpdateId update;
  Price price;
};

class FakeNetwork {
 public:
  SendFn sender () {
    return [this] (PriceUpdateRequest request) {
      std::lock_guard lock (mutex_);
      sent_.push_back (
          SentMessage {request.stationId, request.id, request.price});
      cv_.notify_all ();
    };
  }

  bool wait_for_count (
      std::size_t count,
      std::chrono::milliseconds timeout = std::chrono::seconds (2)) {
    std::unique_lock<std::mutex> lock (mutex_);
    return cv_.wait_for (lock, timeout, [&] { return sent_.size () >= count; });
  }

  std::vector<SentMessage> snapshot () const {
    std::lock_guard<std::mutex> lock (mutex_);
    return sent_;
  }

  std::size_t count () const {
    std::lock_guard<std::mutex> lock (mutex_);
    return sent_.size ();
  }

  // Most recently sent message for `station`, if any has been sent yet.
  std::optional<SentMessage> last_for (StationId station) const {
    std::lock_guard<std::mutex> lock (mutex_);
    auto reversed = sent_ | std::views::reverse;
    auto it = std::ranges::find (reversed, station, &SentMessage::station);
    return it != reversed.end () ? std::optional (*it) : std::nullopt;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<SentMessage> sent_;
};
