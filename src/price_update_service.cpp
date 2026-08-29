#include "price_update_service.hpp"

PriceUpdateService::PriceUpdateService (SendFn send)
    : send_ (std::move (send)) {}

PriceUpdateService::~PriceUpdateService () = default;

void PriceUpdateService::set_price (StationId station, Price price) {
  std::optional<PriceUpdateRequest> newUpdateRequest;
  {
    std::lock_guard lock (m_);
    auto [it, _] = stations_.try_emplace (station);

    auto& client = it->second;

    ++client.nextUpdateId;
    client.nextUpdate = {station, client.nextUpdateId, price};

    if (!client.inFlightUpdateId) {
      client.inFlightUpdateId = client.nextUpdateId;
      newUpdateRequest = client.nextUpdate;
    }
  }

  if (newUpdateRequest) {
    send_ (*newUpdateRequest);
  }
}

void PriceUpdateService::acknowledge (PriceUpdateAck response) {
  std::optional<PriceUpdateRequest> newUpdateRequest;
  {
    std::lock_guard lock (m_);
    auto it = stations_.find (response.stationId);
    if (it == stations_.end ()) {
      return;
    }
    auto& client = it->second;

    if (!client.inFlightUpdateId ||
        response.updateId != *client.inFlightUpdateId) {
      return;
    }

    client.inFlightUpdateId.reset ();

    if (client.nextUpdateId != response.updateId) {
      client.inFlightUpdateId = client.nextUpdateId;
      newUpdateRequest = client.nextUpdate;
    }
  }

  if (newUpdateRequest) {
    send_ (*newUpdateRequest);
  }
}
