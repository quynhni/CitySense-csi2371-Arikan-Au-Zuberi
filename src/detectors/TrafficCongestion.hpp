#pragma once
#include "../core/Aggregator.hpp"
#include "../core/Detector.hpp"

namespace detectors {

class TrafficCongestion {
public:
    TrafficCongestion(double speed_thr, int consecutive_minutes)
        : speed_thr_(speed_thr), consecutive_minutes_(consecutive_minutes) {}

    std::vector<core::DetectionEvent> detect(const core::Aggregator& agg) const {
        auto buckets = agg.zone_buckets();
        std::vector<core::DetectionEvent> events;
        for (const auto& [zone, series] : buckets) {
            int run = 0;
            bool active = false;
            std::size_t active_index = 0;
            std::chrono::system_clock::time_point run_start{};
            for (const auto& [bucket_time, bucket] : series) {
                if (bucket.speed.count == 0 || bucket.speed.mean >= speed_thr_) {
                    run = 0;
                    active = false;
                    continue;
                }
                if (run == 0) {
                    run_start = bucket_time;
                }
                ++run;
                if (run == consecutive_minutes_) {
                    events.push_back({
                        "traffic_congestion",
                        zone,
                        run_start,
                        bucket_time,
                        bucket.speed.mean
                    });
                    active_index = events.size() - 1;
                    active = true;
                } else if (run > consecutive_minutes_ && active) {
                    events[active_index].end = bucket_time;
                    events[active_index].value = bucket.speed.mean;
                }
            }
        }
        return events;
    }

private:
    double speed_thr_;
    int consecutive_minutes_;
};

} // namespace detectors
