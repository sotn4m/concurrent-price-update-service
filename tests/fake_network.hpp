#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <ranges>
#include <vector>

#include "price_update_service.hpp"

class FakeNetwork {
 public:
  SendFn sender () {
    return [this] (PriceUpdateRequest request) {
      {
        std::lock_guard lock (mutex_);
        sent_.push_back (request);
      }
      cv_.notify_all ();
    };
  }

  bool wait_for_count (
      std::size_t count,
      std::chrono::milliseconds timeout = std::chrono::seconds (2)) {
    std::unique_lock<std::mutex> lock (mutex_);
    return cv_.wait_for (lock, timeout, [&] { return sent_.size () >= count; });
  }

  std::vector<PriceUpdateRequest> snapshot () const {
    std::lock_guard<std::mutex> lock (mutex_);
    return sent_;
  }

  std::size_t count () const {
    std::lock_guard<std::mutex> lock (mutex_);
    return sent_.size ();
  }

  // Most recently sent message for `station`, if any has been sent yet.
  std::optional<PriceUpdateRequest> last_for (StationId station) const {
    std::vector<PriceUpdateRequest> messages;
    messages.reserve (sent_.size ());

    {
      std::lock_guard<std::mutex> lock (mutex_);
      messages = sent_;
    }

    auto reversed = messages | std::views::reverse;
    auto it =
        std::ranges::find (reversed, station, &PriceUpdateRequest::stationId);
    return it != reversed.end () ? std::optional (*it) : std::nullopt;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<PriceUpdateRequest> sent_;
};
