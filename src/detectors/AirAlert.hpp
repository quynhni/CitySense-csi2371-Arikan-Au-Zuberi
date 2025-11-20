#pragma once
#include "../core/Aggregator.hpp"
#include "../core/Detector.hpp"

namespace detectors {

class AirAlert {
public:
    AirAlert(double threshold, int min_buckets = 1)
        : threshold_(threshold), min_buckets_(min_buckets) {}

    std::vector<core::DetectionEvent> detect(const core::Aggregator& agg) const {
        auto buckets = agg.zone_buckets();
        std::vector<core::DetectionEvent> events;
        for (const auto& [zone, series] : buckets) {
            int run = 0;
            bool active = false;
            std::size_t active_index = 0;
            std::chrono::system_clock::time_point start{};
            for (const auto& [bucket_time, bucket] : series) {
                if (bucket.pm25.count == 0 || bucket.rolling_pm25_mean <= threshold_) {
                    run = 0;
                    active = false;
                    continue;
                }
                if (run == 0) {
                    start = bucket_time;
                }
                ++run;
                if (run == min_buckets_) {
                    events.push_back({
                        "air_alert",
                        zone,
                        start,
                        bucket_time,
                        bucket.rolling_pm25_mean
                    });
                    active_index = events.size() - 1;
                    active = true;
                } else if (run > min_buckets_ && active) {
                    events[active_index].end = bucket_time;
                    events[active_index].value = bucket.rolling_pm25_mean;
                }
            }
        }
        return events;
    }

private:
    double threshold_;
    int min_buckets_;
};

} // namespace detectors
