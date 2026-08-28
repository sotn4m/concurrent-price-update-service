#include <print>

#include "price_update_service.hpp"

int main() {
    // A trivial "network" that just logs what would be sent. Swap this for a
    // real client when wiring up actual station communication.
    PriceUpdateService service([](StationId station, UpdateId update, Price price) {
        std::println("station={} update={} price={}", station, update, price);
    });

    service.set_price(1, 100);

    return 0;
}
