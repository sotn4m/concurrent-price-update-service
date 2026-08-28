# concurrent-price-update-service

A thread-safe C++ service that propagates price updates from a central
system to a collection of remote stations over an asynchronous,
out-of-order network, while avoiding redundant traffic. See the full
problem statement below.

## Layout

- `src/price_update_service.hpp` — public API (`PriceUpdateService`,
  `StationId`, `UpdateId`, `Price`, `SendFn`). This is the part that's
  considered fixed; supporting types can be extended if useful.
- `src/price_update_service.cpp` — **unimplemented stub** (marked with
  `TODO`s). This is what needs solving.
- `tests/fake_network.hpp` — an in-memory fake of the network layer used by
  tests. It records every message sent via `SendFn` and lets tests control
  timing (`wait_for_count`, `last_for`) instead of sleeping.
- `tests/price_update_service_test.cpp` — GTest scenarios covering the
  requirements below (single send, coalescing while in flight, eventual
  consistency, stale/duplicate acks, independent stations, and concurrency
  stress/race tests). These are expected to fail against the stub.

## Build & test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run with ThreadSanitizer while developing the concurrent parts:

```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

## Problem

Design and implement a thread-safe C++ service responsible for propagating
price updates from a central system to a collection of remote stations.

The central service receives new desired prices for individual stations.
Price updates may arrive at any time, including while a previous update for
the same station is still being processed by the network.

The communication with stations is asynchronous:

- The service sends price updates to stations.
- Updates sent to a station may arrive out of order.
- A station sends an acknowledgement after applying an update.
- Acknowledgements from a given station are guaranteed to arrive in the same
  order in which that station applied the corresponding updates.
- A new desired price may be received before an older price update has been
  acknowledged.

The primary requirement is eventual consistency: every station should
eventually have the latest desired price.

However, sending unnecessary updates should be avoided. If several price
changes occur while an older update is still in flight, the service should
not unnecessarily propagate every intermediate value.

### Example

Suppose the desired price for station A changes as follows:

```
100 -> 120 -> 130
```

while the update for 100 is still in flight. There is no requirement for
station A to apply every intermediate price. The important requirement is
that it eventually reaches `130`.

The implementation should therefore be able to deal with rapidly changing
desired state without creating unnecessary network traffic.
