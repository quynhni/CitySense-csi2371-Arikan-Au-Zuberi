#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <vector>
#include <string>
#include "core/Aggregator.hpp"
#include "detectors/AirAlert.hpp"
#include "detectors/NoiseSpike.hpp"
#include "detectors/TrafficCongestion.hpp"
#include "model/SensorRecord.hpp"

namespace {
model::SensorRecord make_record(int zone, int minute, double speed, double pm25 = 0.0, double db = 0.0) {
    model::SensorRecord r;
    r.zone_id = zone;
    r.sensor_id = "z" + std::to_string(zone) + "_" + std::to_string(minute) + "_" + std::to_string(static_cast<int>(speed));
    r.ts = std::chrono::system_clock::time_point(std::chrono::minutes(200 + minute));
    r.speed = speed;
    if (pm25 > 0.0) {
        r.pm25 = pm25;
    }
    if (db > 0.0) {
        r.db = db;
    }
    return r;
}
}

TEST_CASE("TrafficCongestion detects sustained low speeds") {
    core::Aggregator agg(5);
    std::vector<model::SensorRecord> batch{
        make_record(1, 0, 40.0),
        make_record(1, 1, 20.0),
        make_record(1, 2, 18.0),
        make_record(1, 3, 19.0),
        make_record(2, 1, 50.0)
    };
    agg.consume(batch);

    detectors::TrafficCongestion detector(25.0, 2);
    auto events = detector.detect(agg);
    REQUIRE(events.size() == 1);
    REQUIRE(events.front().type == "traffic_congestion");
    REQUIRE(events.front().zone_id == 1);
    REQUIRE(events.front().start == std::chrono::system_clock::time_point(std::chrono::minutes(201)));
    REQUIRE(events.front().end == std::chrono::system_clock::time_point(std::chrono::minutes(203)));
}

TEST_CASE("AirAlert triggers on high rolling PM2.5") {
    core::Aggregator agg(5);
    std::vector<model::SensorRecord> batch{
        make_record(2, 0, 10.0, 40.0),
        make_record(2, 1, 11.0, 50.0),
        make_record(2, 2, 12.0, 60.0)
    };
    agg.consume(batch);

    detectors::AirAlert detector(35.0, 2);
    auto events = detector.detect(agg);
    REQUIRE_FALSE(events.empty());
    REQUIRE(events.front().type == "air_alert");
    REQUIRE(events.front().zone_id == 2);
}

TEST_CASE("NoiseSpike counts threshold exceedances over sliding window") {
    core::Aggregator agg(5);
    std::vector<model::SensorRecord> batch{
        make_record(3, 0, 20.0, 0.0, 60.0),
        make_record(3, 1, 20.0, 0.0, 75.0),
        make_record(3, 1, 20.0, 0.0, 80.0),
        make_record(3, 2, 20.0, 0.0, 82.0),
        make_record(3, 4, 20.0, 0.0, 65.0)
    };
    agg.consume(batch);

    detectors::NoiseSpike detector(70.0, 3, 3);
    auto events = detector.detect(agg);
    REQUIRE(events.size() == 1);
    REQUIRE(events.front().type == "noise_spike");
    REQUIRE(events.front().zone_id == 3);
    REQUIRE(events.front().value >= 3.0);
}
