#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <vector>
#include <string>
#include "core/Aggregator.hpp"
#include "model/SensorRecord.hpp"

namespace {
using Catch::Approx;

model::SensorRecord rec(int zone, int minute_offset, double speed, double pm25 = 0.0) {
    model::SensorRecord r;
    r.zone_id = zone;
    r.sensor_id = "z" + std::to_string(zone) + "_" + std::to_string(minute_offset);
    r.ts = std::chrono::system_clock::time_point(std::chrono::minutes(100 + minute_offset));
    r.speed = speed;
    if (pm25 > 0.0) {
        r.pm25 = pm25;
    }
    return r;
}
}

TEST_CASE("Aggregator filters, aggregates, and tracks zones") {
    core::Aggregator agg(5);

    std::vector<model::SensorRecord> first{
        rec(1, 0, 10.0, 15.0),
        rec(1, 1, 20.0, 18.0),
        rec(2, 2, 30.0, 25.0)
    };
    agg.consume(first);
    auto summary = agg.summary();
    REQUIRE(summary.total_count == 3);
    REQUIRE(summary.by_zone[1] == 2);
    REQUIRE(summary.by_zone[2] == 1);

    auto buckets = agg.zone_buckets();
    REQUIRE(buckets.count(1) == 1);
    const auto& first_zone = buckets.at(1);
    REQUIRE(first_zone.size() == 2);
    REQUIRE_FALSE(first_zone.empty());
    const auto& bucket = first_zone.begin()->second;
    REQUIRE(bucket.speed.count == 1);
    REQUIRE(bucket.speed.mean == Approx(10.0).margin(0.001));
    REQUIRE(bucket.pm25.count == 1);
    REQUIRE(bucket.pm25.mean == Approx(15.0).margin(0.001));
    REQUIRE(bucket.rolling_pm25_mean >= bucket.pm25.mean);

    model::SensorRecord invalid;
    invalid.zone_id = 0;
    invalid.sensor_id.clear();

    std::vector<model::SensorRecord> second{
        rec(1, 3, 50.0, 10.0),
        rec(2, 3, 12.0, 55.0),
        invalid
    };
    core::Aggregator::PipelineOptions opts;
    opts.start = std::chrono::system_clock::time_point(std::chrono::minutes(103));
    opts.zones = {2};
    agg.consume(second, opts);

    summary = agg.summary();
    REQUIRE(summary.by_zone[2] == 2);
    REQUIRE(summary.by_zone[1] == 2);
    REQUIRE(summary.total_count == 4);
    auto diagnostics = agg.diagnostics();
    REQUIRE_FALSE(diagnostics.empty());
}
