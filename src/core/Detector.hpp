#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace core {

class Aggregator;

struct DetectionEvent {
    std::string type;
    int zone_id{0};
    std::chrono::system_clock::time_point start{};
    std::chrono::system_clock::time_point end{};
    double value{0.0};
};

template <typename T>
concept Detector = requires(T detector, const Aggregator& agg) {
    { detector.detect(agg) } -> std::same_as<std::vector<DetectionEvent>>;
};

class DetectorHandle {
public:
    DetectorHandle() = default;

    template <Detector D>
    explicit DetectorHandle(D detector) {
        assign(std::make_shared<D>(std::move(detector)));
    }

    template <Detector D>
    void reset(D detector) {
        assign(std::make_shared<D>(std::move(detector)));
    }

    bool valid() const {
        return static_cast<bool>(fn_);
    }

    std::vector<DetectionEvent> detect(const Aggregator& agg) const {
        if (!fn_) {
            return {};
        }
        return fn_(agg);
    }

private:
    template <typename D>
    void assign(std::shared_ptr<D> detector) {
        fn_ = [detector](const Aggregator& agg) {
            return detector->detect(agg);
        };
    }

    std::function<std::vector<DetectionEvent>(const Aggregator&)> fn_;
};

template <typename Range>
requires std::ranges::range<Range> && Detector<std::ranges::range_value_t<Range>>
std::vector<DetectionEvent> detect_all(Range& detectors, const Aggregator& agg) {
    std::vector<DetectionEvent> events;
    for (auto& detector : detectors) {
        auto hits = detector.detect(agg);
        events.insert(events.end(), hits.begin(), hits.end());
    }
    return events;
}

} // namespace core
