#include "price_update_service.hpp"

PriceUpdateService::PriceUpdateService (SendFn send)
    : send_ (std::move (send)) {}

PriceUpdateService::~PriceUpdateService () = default;

void PriceUpdateService::set_price (StationId station, Price price) {
  std::lock_guard lock (m_);
  auto [it, _] = stations_.try_emplace (station);

  auto& client = it->second;
  auto send_price_update = client.currentUpdateId == client.lastAckUpdateId;

  ++client.currentUpdateId;
  client.currentPrice = price;

  if (send_price_update) {
    client.inFlightUpdateId = client.currentUpdateId;
    send_ (station, client.currentUpdateId, client.currentPrice);
  }
}

void PriceUpdateService::acknowledge (StationId station, UpdateId updateId) {
  std::lock_guard lock (m_);
  auto it = stations_.find (station);
  if (it == stations_.end ()) {
    return;
  }
  auto& client = it->second;

  if (!client.inFlightUpdateId || updateId != *client.inFlightUpdateId) {
    return;
  }

  client.lastAckUpdateId = updateId;
  client.inFlightUpdateId.reset ();

  if (client.currentUpdateId > client.lastAckUpdateId) {
    client.inFlightUpdateId = client.currentUpdateId;
    send_ (station, client.currentUpdateId, client.currentPrice);
  }
}
