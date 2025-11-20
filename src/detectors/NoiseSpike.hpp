#pragma once
#include <deque>
#include "../core/Aggregator.hpp"
#include "../core/Detector.hpp"

namespace detectors {

class NoiseSpike {
public:
    NoiseSpike(double db_threshold, int count_threshold, int window_minutes)
        : db_threshold_(db_threshold),
          count_threshold_(count_threshold),
          window_minutes_(window_minutes) {}

    std::vector<core::DetectionEvent> detect(const core::Aggregator& agg) const {
        auto buckets = agg.zone_buckets();
        std::vector<core::DetectionEvent> events;
        for (const auto& [zone, series] : buckets) {
            std::deque<std::pair<std::chrono::system_clock::time_point, int>> history;
            int rolling_total = 0;
            bool active = false;
            std::size_t active_index = 0;
            for (const auto& [bucket_time, bucket] : series) {
                int exceed = 0;
                for (double sample : bucket.db_samples) {
                    if (sample > db_threshold_) {
                        ++exceed;
                    }
                }
                if (exceed > 0) {
                    history.push_back({bucket_time, exceed});
                    rolling_total += exceed;
                }
                auto cutoff = bucket_time - std::chrono::minutes(window_minutes_);
                while (!history.empty() && history.front().first < cutoff) {
                    rolling_total -= history.front().second;
                    history.pop_front();
                }
                if (rolling_total >= count_threshold_ && !active && !history.empty()) {
                    events.push_back({
                        "noise_spike",
                        zone,
                        history.front().first,
                        bucket_time,
                        static_cast<double>(rolling_total)
                    });
                    active_index = events.size() - 1;
                    active = true;
                } else if (rolling_total >= count_threshold_ && active) {
                    events[active_index].end = bucket_time;
                    events[active_index].value = static_cast<double>(rolling_total);
                } else if (rolling_total < count_threshold_) {
                    active = false;
                }
            }
        }
        return events;
    }

private:
    double db_threshold_;
    int count_threshold_;
    int window_minutes_;
};

} // namespace detectors
