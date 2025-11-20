#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Window.hpp"
#include "../model/SensorView.hpp"

namespace core {

struct Summary {
    int total_count{0};
    std::unordered_map<int,int> by_zone;
};

struct MetricStats {
    int count{0};
    double mean{0.0};
    double median{0.0};
    double p90{0.0};
    double p99{0.0};
};

struct BucketAggregate {
    std::chrono::system_clock::time_point bucket_start{};
    MetricStats speed;
    MetricStats pm25;
    MetricStats db;
    double rolling_speed_mean{0.0};
    double rolling_pm25_mean{0.0};
    std::vector<double> speed_samples;
    std::vector<double> pm25_samples;
    std::vector<double> db_samples;
};

class Aggregator {
public:
    struct PipelineOptions {
        std::optional<std::chrono::system_clock::time_point> start;
        std::optional<std::chrono::system_clock::time_point> end;
        std::vector<int> zones;
        bool input_speed_mps{false};
        bool input_pm25_mg{false};
    };

    explicit Aggregator(int window_minutes)
        : window_(std::chrono::minutes{window_minutes}),
          rolling_span_(std::chrono::minutes{window_minutes}) {}

    template <typename Range>
    void consume(const Range& range, const PipelineOptions& options = {}) {
        using value_t = std::ranges::range_value_t<Range>;
        static_assert(std::same_as<std::remove_cv_t<value_t>, model::SensorRecord> ||
                      std::same_as<std::remove_cv_t<value_t>, model::SensorView>,
                      "Range must contain SensorRecord or SensorView");

        std::vector<model::SensorRecord> accepted;
        std::vector<std::string> diag_buffer;
        if constexpr (std::ranges::sized_range<Range>) {
            accepted.reserve(std::ranges::size(range));
        }

        std::unordered_set<int> allowed_zones(options.zones.begin(), options.zones.end());

        auto zone_match = [&](int zone) {
            return allowed_zones.empty() || allowed_zones.count(zone) > 0;
        };

        for (const auto& entry : range) {
            auto rec_opt = materialize(entry);
            if (!rec_opt) {
                diag_buffer.push_back("dropped record: unsupported input");
                continue;
            }
            auto rec = std::move(*rec_opt);
            if (!is_valid(rec)) {
                diag_buffer.push_back(diag_for(rec, "malformed record"));
                continue;
            }
            if (!in_time_range(rec.ts, options)) {
                continue;
            }
            if (!zone_match(rec.zone_id)) {
                continue;
            }
            apply_conversions(rec, options);
            accepted.push_back(std::move(rec));
        }

        std::scoped_lock lock(mutex_);
        append_diagnostics(diag_buffer);
        if (accepted.empty() && window_.records().empty()) {
            return;
        }
        for (auto& rec : accepted) {
            window_.push(std::move(rec));
            if (rec.ts > latest_ts_) {
                latest_ts_ = rec.ts;
            }
        }
        if (latest_ts_ != std::chrono::system_clock::time_point{}) {
            window_.trim(latest_ts_);
        }
        rebuild_locked();
    }

    Window current_window_view() const {
        std::scoped_lock lock(mutex_);
        return window_;
    }

    Summary summary() const {
        std::scoped_lock lock(mutex_);
        return summary_;
    }

    std::vector<std::string> diagnostics() const {
        std::scoped_lock lock(mutex_);
        return diagnostics_;
    }

    std::unordered_map<int, std::map<std::chrono::system_clock::time_point, BucketAggregate>> zone_buckets() const {
        std::scoped_lock lock(mutex_);
        return zone_buckets_;
    }

private:
    template <typename Entry>
    static std::optional<model::SensorRecord> materialize(const Entry& entry) {
        using entry_t = std::remove_cvref_t<Entry>;
        if constexpr (std::same_as<entry_t, model::SensorRecord>) {
            return entry;
        } else if constexpr (std::same_as<entry_t, model::SensorView>) {
            model::SensorRecord rec;
            rec.ts = entry.ts;
            rec.sensor_id = std::string(entry.sensor_id);
            rec.zone_id = entry.zone_id;
            rec.speed = entry.speed;
            rec.flow = entry.flow;
            rec.pm25 = entry.pm25;
            rec.pm10 = entry.pm10;
            rec.db = entry.db;
            return rec;
        } else {
            return std::nullopt;
        }
    }

    static bool is_valid(const model::SensorRecord& rec) {
        return rec.zone_id > 0 &&
               !rec.sensor_id.empty() &&
               rec.ts != std::chrono::system_clock::time_point{};
    }

    static bool in_time_range(
        std::chrono::system_clock::time_point ts,
        const PipelineOptions& options) {
        if (options.start && ts < *options.start) {
            return false;
        }
        if (options.end && ts > *options.end) {
            return false;
        }
        return true;
    }

    static void apply_conversions(model::SensorRecord& rec, const PipelineOptions& options) {
        if (options.input_speed_mps && rec.speed) {
            rec.speed = *rec.speed * 3.6;
        }
        if (options.input_pm25_mg && rec.pm25) {
            rec.pm25 = *rec.pm25 * 1000.0;
        }
    }

    static std::string diag_for(const model::SensorRecord& rec, std::string label) {
        std::ostringstream oss;
        oss << label << " zone=" << rec.zone_id << " sensor=" << rec.sensor_id;
        return oss.str();
    }

    static MetricStats make_stats(std::vector<double> values) {
        MetricStats stats;
        if (values.empty()) {
            return stats;
        }
        stats.count = static_cast<int>(values.size());
        double sum = 0.0;
        for (double v : values) {
            sum += v;
        }
        stats.mean = sum / static_cast<double>(values.size());
        std::sort(values.begin(), values.end());
        stats.median = median(values);
        stats.p90 = percentile(values, 0.90);
        stats.p99 = percentile(values, 0.99);
        return stats;
    }

    static double median(const std::vector<double>& values) {
        if (values.empty()) {
            return 0.0;
        }
        const auto n = values.size();
        if (n % 2 == 1) {
            return values[n / 2];
        }
        return (values[n / 2 - 1] + values[n / 2]) / 2.0;
    }

    static double percentile(const std::vector<double>& values, double ratio) {
        if (values.empty()) {
            return 0.0;
        }
        const double idx = ratio * (values.size() - 1);
        const auto lower = static_cast<std::size_t>(std::floor(idx));
        const auto upper = static_cast<std::size_t>(std::ceil(idx));
        if (lower == upper) {
            return values[lower];
        }
        const double fraction = idx - static_cast<double>(lower);
        return values[lower] + (values[upper] - values[lower]) * fraction;
    }

    static std::chrono::system_clock::time_point bucket_floor(std::chrono::system_clock::time_point ts) {
        const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(ts.time_since_epoch());
        return std::chrono::system_clock::time_point(minutes);
    }

    void append_diagnostics(const std::vector<std::string>& batch) {
        for (const auto& msg : batch) {
            diagnostics_.push_back(msg);
            if (diagnostics_.size() > diag_limit_) {
                diagnostics_.erase(diagnostics_.begin());
            }
        }
    }

    void rebuild_locked() {
        Summary new_summary;
        std::unordered_map<int, std::map<std::chrono::system_clock::time_point, BucketAggregate>> new_zone_buckets;

        for (const auto& rec : window_.records()) {
            ++new_summary.total_count;
            ++new_summary.by_zone[rec.zone_id];
            auto bucket_time = bucket_floor(rec.ts);
            auto& bucket = new_zone_buckets[rec.zone_id][bucket_time];
            bucket.bucket_start = bucket_time;
            if (rec.speed) {
                bucket.speed_samples.push_back(*rec.speed);
            }
            if (rec.pm25) {
                bucket.pm25_samples.push_back(*rec.pm25);
            }
            if (rec.db) {
                bucket.db_samples.push_back(*rec.db);
            }
        }

        for (auto& [zone, series] : new_zone_buckets) {
            std::deque<std::pair<std::chrono::system_clock::time_point, double>> speed_window;
            std::deque<std::pair<std::chrono::system_clock::time_point, double>> pm_window;
            for (auto& [bucket_time, bucket] : series) {
                bucket.speed = make_stats(bucket.speed_samples);
                bucket.pm25 = make_stats(bucket.pm25_samples);
                bucket.db = make_stats(bucket.db_samples);

                if (bucket.speed.count > 0) {
                    speed_window.push_back({bucket_time, bucket.speed.mean});
                }
                if (bucket.pm25.count > 0) {
                    pm_window.push_back({bucket_time, bucket.pm25.mean});
                }
                trim_window(speed_window, bucket_time);
                trim_window(pm_window, bucket_time);
                bucket.rolling_speed_mean = average_window(speed_window);
                bucket.rolling_pm25_mean = average_window(pm_window);
            }
        }

        summary_ = std::move(new_summary);
        zone_buckets_ = std::move(new_zone_buckets);
    }

    void trim_window(std::deque<std::pair<std::chrono::system_clock::time_point, double>>& window,
                     std::chrono::system_clock::time_point current) const {
        const auto cutoff = current - rolling_span_;
        while (!window.empty() && window.front().first < cutoff) {
            window.pop_front();
        }
    }

    static double average_window(const std::deque<std::pair<std::chrono::system_clock::time_point, double>>& window) {
        if (window.empty()) {
            return 0.0;
        }
        double sum = 0.0;
        for (const auto& entry : window) {
            sum += entry.second;
        }
        return sum / static_cast<double>(window.size());
    }

    Window window_;
    std::chrono::minutes rolling_span_;
    const std::size_t diag_limit_{128};
    mutable std::mutex mutex_;
    std::chrono::system_clock::time_point latest_ts_{};
    Summary summary_;
    std::vector<std::string> diagnostics_;
    std::unordered_map<int, std::map<std::chrono::system_clock::time_point, BucketAggregate>> zone_buckets_;
};

} // namespace core
