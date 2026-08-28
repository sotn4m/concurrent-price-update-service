#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

using StationId = std::uint64_t;

using UpdateId = std::uint64_t;

using Price = std::int64_t;

using SendFn =
    std::function<void (StationId station, UpdateId update, Price price)>;

class PriceUpdateService {
 public:
  explicit PriceUpdateService (SendFn send);

  ~PriceUpdateService ();

  PriceUpdateService (const PriceUpdateService&) = delete;
  PriceUpdateService& operator= (const PriceUpdateService&) = delete;

  void set_price (StationId station, Price price);

  void acknowledge (StationId station, UpdateId update);

 private:
  struct PriceStationClient {
    UpdateId currentUpdateId {0};
    std::optional<uint64_t> inFlightUpdateId {};
    UpdateId lastAckUpdateId {0};
    Price currentPrice {0};
  };

  std::mutex m_;
  std::unordered_map<StationId, PriceStationClient> stations_;
  SendFn send_;
};
