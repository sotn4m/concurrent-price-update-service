#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

using StationId = std::uint64_t;

using UpdateId = std::uint64_t;

using Price = std::int64_t;

struct PriceUpdateAck {
  StationId stationId {0};
  UpdateId updateId {0};
};

struct PriceUpdateRequest {
  StationId stationId {0};
  UpdateId id {0};
  Price price {0};
};

using SendFn = std::function<void (PriceUpdateRequest update)>;

class PriceUpdateService {
 public:
  explicit PriceUpdateService (SendFn send);

  ~PriceUpdateService ();

  PriceUpdateService (const PriceUpdateService&) = delete;
  PriceUpdateService& operator= (const PriceUpdateService&) = delete;

  void set_price (StationId station, Price price);

  void acknowledge (PriceUpdateAck response);

 private:
  struct PriceStationClient {
    UpdateId nextUpdateId {0};
    PriceUpdateRequest nextUpdate {};

    std::optional<uint64_t> inFlightUpdateId {};
    UpdateId lastAckUpdateId {0};
  };

  std::mutex m_;
  std::unordered_map<StationId, PriceStationClient> stations_;
  SendFn send_;
};
