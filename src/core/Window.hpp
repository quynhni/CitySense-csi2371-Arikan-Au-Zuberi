#pragma once
#include <algorithm>
#include <chrono>
#include <vector>
#include "../model/SensorRecord.hpp"

namespace core {

class Window {
public:
    explicit Window(std::chrono::minutes span = std::chrono::minutes{5})
        : span_(span) {}

    void push(model::SensorRecord record) {
        records_.push_back(std::move(record));
    }

    void trim(std::chrono::system_clock::time_point latest) {
        if (records_.empty()) {
            return;
        }
        const auto cutoff = latest - span_;
        auto it = std::remove_if(records_.begin(), records_.end(),
            [&](const model::SensorRecord& rec) { return rec.ts < cutoff; });
        records_.erase(it, records_.end());
    }

    const std::vector<model::SensorRecord>& records() const {
        return records_;
    }

    std::chrono::minutes span() const {
        return span_;
    }

    void set_span(std::chrono::minutes span) {
        span_ = span;
    }

private:
    std::chrono::minutes span_;
    std::vector<model::SensorRecord> records_;
};

} // namespace core
