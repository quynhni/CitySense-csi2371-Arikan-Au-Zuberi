#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <vector>
#include <string>
#include "core/Aggregator.hpp"
#include "core/Detector.hpp"
#include "model/SensorRecord.hpp"
#include "io/ReaderCSV.hpp"

namespace {
model::SensorRecord mk(int zone) {
    model::SensorRecord r;
    r.zone_id = zone;
    r.sensor_id = "s" + std::to_string(zone);
    r.ts = std::chrono::system_clock::time_point(std::chrono::minutes(zone));
    r.speed = 10.0 * zone;
    return r;
}
}

struct DummyDetector {
    std::vector<core::DetectionEvent> detect(const core::Aggregator& agg) {
        auto buckets = agg.zone_buckets();
        if (buckets.empty()) {
            return {};
        }
        return { core::DetectionEvent{ "dummy", buckets.begin()->first, {}, {}, 0.0 } };
    }
};

TEST_CASE("Public API surfaces exist (types, methods)") {
    core::Aggregator agg(5);

    agg.consume(std::vector<model::SensorRecord>{ mk(1), mk(2) });
    auto s = agg.summary();
    REQUIRE(s.total_count >= 1);

    std::vector<DummyDetector> detectors{ DummyDetector{} };
    auto events = core::detect_all(detectors, agg);
    REQUIRE(events.size() <= detectors.size());

    io::ReaderCSV reader({ "data/air.csv" });
    auto batch = reader.next_batch(10);
    REQUIRE(batch.size() <= 10);
}
