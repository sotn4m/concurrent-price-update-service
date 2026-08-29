#include "price_update_service.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <ranges>
#include <stop_token>
#include <thread>
#include <vector>

#include "fake_network.hpp"

namespace {

class PriceUpdateServiceTest : public ::testing::Test {
 protected:
  FakeNetwork net;
  PriceUpdateService svc {net.sender ()};
};

// Asserts that no further message shows up within `wait` beyond
// `expected_count` already sent. Waits only as long as it takes for a
// violation to appear (or `wait` elapses), rather than always sleeping the
// full duration.
void expect_no_additional_sends (
    FakeNetwork& net,
    std::size_t expected_count,
    std::chrono::milliseconds wait = std::chrono::milliseconds (50)) {
  net.wait_for_count (expected_count + 1, wait);
  EXPECT_EQ (net.count (), expected_count);
}

// Repeatedly acknowledges whatever is currently the latest sent update for
// `station`, until the acknowledged price reaches `target` or `timeout`
// elapses. Models a network that keeps delivering the newest in-flight
// update to the station and the station keeps acking it. Returns the last
// price actually acknowledged (std::numeric_limits<Price>::min() if none).
template <typename Service>
Price drain_to (Service& svc,
                FakeNetwork& net,
                StationId station,
                Price target,
                std::chrono::milliseconds timeout = std::chrono::seconds (2)) {
  const auto deadline = std::chrono::steady_clock::now () + timeout;
  Price last_acked = std::numeric_limits<Price>::min ();
  UpdateId last_update_acked {};
  bool have_acked = false;

  while (std::chrono::steady_clock::now () < deadline) {
    auto last = net.last_for (station);
    if (last && (!have_acked || last->update != last_update_acked)) {
      svc.acknowledge (PriceUpdateAck {station, last->update});
      last_acked = last->price;
      last_update_acked = last->update;
      have_acked = true;
      if (last_acked == target) {
        break;
      }
    }
    net.wait_for_count (net.count () + 1, std::chrono::milliseconds (20));
  }
  return last_acked;
}

}  // namespace

TEST_F (PriceUpdateServiceTest, SingleSetPriceSendsExactlyOneUpdate) {
  auto& net = this->net;
  auto& svc = this->svc;

  svc.set_price (1, 100);

  ASSERT_TRUE (net.wait_for_count (1));
  auto msgs = net.snapshot ();
  ASSERT_EQ (msgs.size (), 1u);
  EXPECT_EQ (msgs[0].station, 1u);
  EXPECT_EQ (msgs[0].price, 100);

  svc.acknowledge (PriceUpdateAck {1, msgs[0].update});

  // Nothing else was pending, so acking shouldn't trigger another send.
  expect_no_additional_sends (net, 1);
}

TEST_F (PriceUpdateServiceTest, CoalescesRapidUpdatesWhileInFlight) {
  auto& net = this->net;
  auto& svc = this->svc;

  svc.set_price (1, 100);
  ASSERT_TRUE (net.wait_for_count (1));

  // While the first update is still in flight (unacknowledged), fire two
  // more price changes for the same station.
  svc.set_price (1, 120);
  svc.set_price (1, 130);

  // The service must not send anything new until the in-flight update is
  // acknowledged -- it has nowhere to put a second concurrent update.
  expect_no_additional_sends (net, 1);

  // Acking the first update should trigger exactly one more send, and it
  // should carry the latest price (130), skipping the intermediate 120.
  svc.acknowledge (PriceUpdateAck {1, net.snapshot ()[0].update});
  ASSERT_TRUE (net.wait_for_count (2));

  auto msgs = net.snapshot ();
  ASSERT_EQ (msgs.size (), 2u);
  EXPECT_EQ (msgs[1].price, 130);
  for (const auto& m : msgs) {
    EXPECT_NE (m.price, 120)
        << "an intermediate price should never be put on the wire";
  }

  svc.acknowledge (PriceUpdateAck {1, msgs[1].update});
  expect_no_additional_sends (net, 2);
}

TEST_F (PriceUpdateServiceTest,
            EventuallyConvergesToLatestPriceDespiteManyChanges) {
  auto& net = this->net;
  auto& svc = this->svc;

  constexpr Price kFinal = 999;
  for (Price p = 1; p <= kFinal; ++p) {
    svc.set_price (1, p);
  }
  ASSERT_TRUE (net.wait_for_count (1));

  Price converged = drain_to (svc, net, 1, kFinal);
  EXPECT_EQ (converged, kFinal);
}

TEST_F (PriceUpdateServiceTest,
            StaleOrDuplicateAcknowledgementIsIgnoredSafely) {
  auto& net = this->net;
  auto& svc = this->svc;

  svc.set_price (1, 100);
  ASSERT_TRUE (net.wait_for_count (1));
  const auto first = net.snapshot ()[0];

  svc.set_price (
      1, 200);  // superseding value, still queued behind the in-flight send
  expect_no_additional_sends (net, 1);

  svc.acknowledge (PriceUpdateAck {1, first.update});
  ASSERT_TRUE (net.wait_for_count (2));
  const auto second = net.snapshot ()[1];
  EXPECT_EQ (second.price, 200);

  // A late duplicate of the first ack must not resend or corrupt state.
  svc.acknowledge (PriceUpdateAck {1, first.update});
  // An unknown/bogus update id must also be handled gracefully (no crash,
  // no spurious send, and it must not corrupt tracking of the real
  // in-flight update below).
  svc.acknowledge (PriceUpdateAck {1, first.update + 1'000'000});
  expect_no_additional_sends (net, 2);

  // The real ack for the still-outstanding update must still be honored
  // even after the bogus/duplicate ones above.
  svc.acknowledge (PriceUpdateAck {1, second.update});
  expect_no_additional_sends (net, 2);

  // And the station must still be responsive to new prices afterward.
  svc.set_price (1, 300);
  ASSERT_TRUE (net.wait_for_count (3));
  EXPECT_EQ (net.last_for (1)->price, 300);
}

TEST_F (PriceUpdateServiceTest, IndependentStationsDoNotBlockEachOther) {
  auto& net = this->net;
  auto& svc = this->svc;

  constexpr StationId kSlow = 1;
  constexpr StationId kFast = 2;

  svc.set_price (kSlow, 100);  // left in flight, never acknowledged
  ASSERT_TRUE (net.wait_for_count (1));

  // An unrelated station must still get its update sent promptly, even
  // though kSlow has a permanently outstanding update.
  svc.set_price (kFast, 500);
  ASSERT_TRUE (net.wait_for_count (2, std::chrono::milliseconds (500)));

  auto fast_msg = net.last_for (kFast);
  ASSERT_TRUE (fast_msg.has_value ());
  EXPECT_EQ (fast_msg->price, 500);
}

TEST_F (PriceUpdateServiceTest,
            RaceBetweenSetPriceAndAcknowledgeNeverLosesUpdate) {
  auto& net = this->net;
  auto& svc = this->svc;

  svc.set_price (1, 1);
  ASSERT_TRUE (net.wait_for_count (1));
  const UpdateId first_update = net.snapshot ()[0].update;

  constexpr Price kFinal = 42;

  // Race: one thread acknowledges the in-flight update while another
  // thread supersedes it with a new price, at (roughly) the same time.
  std::thread acker ([&] { svc.acknowledge (PriceUpdateAck {1, first_update}); });
  std::thread setter ([&] { svc.set_price (1, kFinal); });
  acker.join ();
  setter.join ();

  // Whatever the interleaving, the final price must not be silently
  // dropped: draining acknowledgements must eventually reach it.
  Price converged = drain_to (svc, net, 1, kFinal);
  EXPECT_EQ (converged, kFinal);
}

TEST_F (PriceUpdateServiceTest, ConcurrentMultiStationStressConverges) {
  auto& net = this->net;
  auto& svc = this->svc;

  constexpr int kStations = 8;
  constexpr Price kFinal = 200;

  auto station_ids = std::views::iota (StationId {0}, StationId {kStations});

  std::vector<std::jthread> writers;
  writers.reserve (kStations);
  for (StationId s : station_ids) {
    writers.emplace_back ([&svc, s] {
      for (Price p = 1; p <= kFinal; ++p) {
        svc.set_price (s, p);
      }
    });
  }

  // Simulates a network that keeps delivering whatever is currently the
  // latest in-flight update per station, concurrently with producers still
  // calling set_price(). Re-acknowledging an id the service has already
  // moved past is harmless -- acknowledge() ignores anything that isn't
  // exactly the current in-flight update -- so this doesn't need to track
  // what it already acked.
  std::jthread acker ([&] (std::stop_token st) {
    while (!st.stop_requested ()) {
      for (StationId s : station_ids) {
        if (auto last = net.last_for (s)) {
          svc.acknowledge (PriceUpdateAck {s, last->update});
        }
      }
      std::this_thread::sleep_for (std::chrono::microseconds (200));
    }
  });

  for (auto& t : writers) {
    t.join ();
  }

  const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
  bool converged = false;
  while (std::chrono::steady_clock::now () < deadline) {
    converged = std::ranges::all_of (station_ids, [&] (StationId s) {
      auto last = net.last_for (s);
      return last.has_value () && last->price == kFinal;
    });
    if (converged) {
      break;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (20));
  }

  acker.request_stop ();
  acker.join ();

  EXPECT_TRUE (converged) << "not every station's latest sent price reached "
                          << kFinal;
}
