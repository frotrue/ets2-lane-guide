#include "ingame_signals.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lane {
namespace {

// ETS2LA/plugin commit 7094b334f10b68343d0082f1ef4078669235106b:
// src/core.hpp, src/prism/common.cpp, src/memory/virtual/memory_handler.cpp.
// These are offsets into a published packed output buffer, not game addresses.
constexpr std::size_t kSlotBytes = 48;
constexpr std::size_t kSlotCount = 40;
constexpr std::size_t kSnapshotBytes = kSlotBytes * kSlotCount;
constexpr int kPluginVersion = 1610; // stoi("1.61.0" with dots removed), as upstream.
constexpr std::uint64_t kFreshMs = 750;
constexpr std::uint64_t kProgressMs = 1000;

bool supportedGame(const std::string& version) {
    return version == "1.61" || version.compare(0, 5, "1.61.") == 0;
}

template<class T>
T readPacked(const std::uint8_t* bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes + offset, sizeof(T));
    return value;
}

struct Observation {
    int id = 0;
    SignalPosition position{};
    double remaining = 0;
    int state = 0;
    std::optional<std::uint64_t> progressAt;
};

bool knownState(int state) {
    return state == 0 || state == 1 || state == 2 || state == 4 || state == 8 || state == 32;
}
bool displayState(int state) {
    return state == 1 || state == 2 || state == 4 || state == 8;
}
bool nextState(int oldState, int newState) {
    return (oldState == 1 && newState == 2) ||
           (oldState == 2 && (newState == 4 || newState == 8)) ||
           (oldState == 4 && newState == 8) || (oldState == 8 && newState == 1);
}
bool finitePosition(const SignalPosition& position) {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

bool decode(const std::uint8_t* bytes, std::size_t size, std::vector<Observation>& result) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                  "ETS2LA records require IEEE-754 float32");
    if (!bytes || size != kSnapshotBytes) return false;
    // Supported production target is little-endian Windows x64.
    const std::uint16_t endianCheck = 1;
    if (*reinterpret_cast<const std::uint8_t*>(&endianCheck) != 1) return false;
    result.clear();
    result.reserve(kSlotCount);
    for (std::size_t offset = 0; offset < size; offset += kSlotBytes) {
        const auto* slot = bytes + offset;
        const auto type = readPacked<std::int32_t>(slot, 32);
        if (type == 0 || type == 2) continue; // Empty slot or barrier/gate.
        if (type != 1) return false;
        Observation signal;
        signal.position = {
            static_cast<double>(readPacked<float>(slot, 0)) + readPacked<std::int16_t>(slot, 12) * 512.0,
            readPacked<float>(slot, 4),
            static_cast<double>(readPacked<float>(slot, 8)) + readPacked<std::int16_t>(slot, 14) * 512.0
        };
        double normSquared = 0;
        for (const std::size_t index : {16u, 20u, 24u, 28u}) {
            const double component = readPacked<float>(slot, index);
            if (!std::isfinite(component)) return false;
            normSquared += component * component;
        }
        const double norm = std::sqrt(normSquared);
        signal.remaining = readPacked<float>(slot, 36);
        signal.state = readPacked<std::int32_t>(slot, 40);
        signal.id = readPacked<std::int32_t>(slot, 44);
        if (!finitePosition(signal.position) || !std::isfinite(signal.remaining) ||
            std::max({std::abs(signal.position.x), std::abs(signal.position.y), std::abs(signal.position.z)}) > 1e7 ||
            signal.remaining < 0 || signal.remaining > 3600 || signal.id < 0 ||
            norm < 0.99 || norm > 1.01 || !knownState(signal.state)) return false;
        result.push_back(signal);
    }
    return true;
}

std::string identityCoordinate(double value) {
    // Match JS toFixed(2)'s rounding of the exact binary value, including .125
    // ties and negative values that round to zero. iostream fixed formatting
    // uses ties-to-even instead, and multiplying a double by 100 can round a
    // just-below-half value onto the half. Positions are already bounded to 1e7.
    int exponent = 0;
    const double fraction = std::frexp(std::abs(value), &exponent);
    const auto mantissa = static_cast<std::uint64_t>(std::ldexp(fraction, 53));
    const std::uint64_t numerator = mantissa * 100;
    const int shift = 53 - exponent;
    std::uint64_t rounded = 0;
    if (shift < 64) {
        rounded = numerator >> shift;
        const std::uint64_t remainder = numerator & ((std::uint64_t{1} << shift) - 1);
        if (remainder >= (std::uint64_t{1} << (shift - 1))) ++rounded;
    }
    return std::string(value < 0 ? "-" : "+") + std::to_string(rounded);
}

std::string identity(const Observation& signal) {
    return std::to_string(signal.id) + ':' + identityCoordinate(signal.position.x) + ':' +
           identityCoordinate(signal.position.y) + ':' + identityCoordinate(signal.position.z);
}

class Receiver {
public:
    void clear() {
        signals_.clear(); previous_.clear(); receivedAt_.reset(); sequence_ = 0;
    }

    bool ingest(const std::uint8_t* bytes, std::size_t size, int pluginVersion,
                std::uint64_t now, std::uint64_t sequence) {
        std::vector<Observation> signals;
        const bool sequenceWindow = receivedAt_ && (now < *receivedAt_ || now - *receivedAt_ < 1500);
        if (pluginVersion != kPluginVersion ||
            (sequenceWindow && sequence <= sequence_) || !decode(bytes, size, signals)) {
            clear(); return false;
        }
        const bool usePrevious = receivedAt_ && now >= *receivedAt_ && now - *receivedAt_ <= kFreshMs;
        std::unordered_map<std::string, Observation> next;
        for (auto& signal : signals) {
            const auto key = identity(signal);
            const auto old = previous_.find(key);
            if (usePrevious && old != previous_.end() && now > *receivedAt_) {
                const double decrease = old->second.remaining - signal.remaining;
                const double elapsed = static_cast<double>(now - *receivedAt_) / 1000.0;
                const bool reasonableDecrease = decrease > 0.001 && decrease <= elapsed * 4 + 0.1;
                if ((signal.state == old->second.state && reasonableDecrease) ||
                    (signal.state != old->second.state && nextState(old->second.state, signal.state))) {
                    signal.progressAt = now;
                } else if (signal.state == old->second.state && std::abs(decrease) <= 0.001) {
                    signal.progressAt = old->second.progressAt;
                }
            }
            // Keep all runtime copies for conflict checks; the last identity is
            // the baseline for the next sample, matching the JS receiver.
            next[key] = signal;
        }
        signals_ = std::move(signals); previous_ = std::move(next);
        receivedAt_ = now; sequence_ = sequence;
        return true;
    }

    SignalResult select(const std::vector<SignalTarget>& targets, const std::string& gameVersion,
                        std::uint64_t now) const {
        if (!supportedGame(gameVersion) || !receivedAt_ || now < *receivedAt_ || now - *receivedAt_ > kFreshMs ||
            targets.empty() || targets.size() > kSlotCount) return {};
        std::array<bool, kSlotCount> selected{};
        for (const auto& target : targets) {
            if (target.id < 0 || target.positions.empty() ||
                std::any_of(target.positions.begin(), target.positions.end(), [](const SignalPosition& p) {
                    return !finitePosition(p);
                })) return {};
            bool matched = false;
            for (std::size_t index = 0; index < signals_.size(); ++index) {
                const auto& signal = signals_[index];
                if (signal.id != target.id) continue;
                const bool atLocator = std::any_of(target.positions.begin(), target.positions.end(), [&](const SignalPosition& p) {
                    return std::hypot(signal.position.x - p.x, signal.position.y - p.y, signal.position.z - p.z) <= 2;
                });
                if (atLocator) { selected[index] = true; matched = true; }
            }
            if (!matched) return {}; // Every route-controlled group is required.
        }
        int state = 0;
        double lowest = std::numeric_limits<double>::infinity(), highest = -lowest;
        for (std::size_t index = 0; index < signals_.size(); ++index) {
            if (!selected[index]) continue;
            const auto& signal = signals_[index];
            if (!displayState(signal.state) || !signal.progressAt || now < *signal.progressAt ||
                now - *signal.progressAt > kProgressMs || (state && state != signal.state)) return {};
            state = signal.state;
            lowest = std::min(lowest, signal.remaining); highest = std::max(highest, signal.remaining);
        }
        if (!state || highest - lowest > 0.25) return {};
        return {true, state, static_cast<int>(std::ceil(lowest))};
    }
private:
    std::vector<Observation> signals_;
    std::unordered_map<std::string, Observation> previous_;
    std::optional<std::uint64_t> receivedAt_;
    std::uint64_t sequence_ = 0;
};

#if defined(_WIN32)
bool copyMapping(const wchar_t* name, std::uint8_t* output, std::size_t bytes) {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!mapping) return false;
    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, bytes);
    if (!view) { CloseHandle(mapping); return false; }
    std::memcpy(output, view, bytes);
    UnmapViewOfFile(view); CloseHandle(mapping);
    return true;
}
#endif

} // namespace

struct SignalReader::Impl {
    Receiver receiver;
    std::uint64_t sequence = 0;
};
SignalReader::SignalReader() : impl_(std::make_unique<Impl>()) {}
SignalReader::~SignalReader() = default;
void SignalReader::clear() { impl_->receiver.clear(); impl_->sequence = 0; }
SignalResult SignalReader::update(const std::vector<SignalTarget>& targets, bool active,
                                  const std::string& gameVersion) {
    if (!active || !supportedGame(gameVersion)) { clear(); return {}; }
#if defined(_WIN32)
    std::array<std::uint8_t, 6> status{}, statusAgain{};
    std::array<std::uint8_t, kSnapshotBytes> snapshot{}, snapshotAgain{};
    // Close every mapping immediately. A stopped producer cannot be kept alive
    // by our handle. The public format has no sequence/lock: reject visibly
    // inconsistent copies, then require observed timer progress independently.
    const bool available =
        copyMapping(L"Local\\ETS2LAPluginStatus", status.data(), status.size()) &&
        copyMapping(L"Local\\ETS2LASemaphore", snapshot.data(), snapshot.size()) &&
        copyMapping(L"Local\\ETS2LASemaphore", snapshotAgain.data(), snapshotAgain.size()) &&
        copyMapping(L"Local\\ETS2LAPluginStatus", statusAgain.data(), statusAgain.size()) &&
        status == statusAgain && snapshot == snapshotAgain;
    if (!available) { clear(); return {}; }
    const auto now = static_cast<std::uint64_t>(GetTickCount64());
    if (!impl_->receiver.ingest(snapshot.data(), snapshot.size(), readPacked<std::int32_t>(status.data(), 0),
                                now, ++impl_->sequence)) return {};
    return impl_->receiver.select(targets, gameVersion, now);
#else
    (void)targets;
    clear(); return {}; // Deterministic snapshot tests remain portable.
#endif
}

namespace signal_test {
struct SnapshotReceiver::Impl { Receiver receiver; };
SnapshotReceiver::SnapshotReceiver() : impl_(std::make_unique<Impl>()) {}
SnapshotReceiver::~SnapshotReceiver() = default;
SnapshotReceiver::SnapshotReceiver(SnapshotReceiver&&) noexcept = default;
SnapshotReceiver& SnapshotReceiver::operator=(SnapshotReceiver&&) noexcept = default;
void SnapshotReceiver::clear() { impl_->receiver.clear(); }
bool SnapshotReceiver::ingestSnapshot(const std::uint8_t* data, std::size_t size, int pluginVersion,
                                      std::uint64_t nowMs, std::uint64_t sequence) {
    return impl_->receiver.ingest(data, size, pluginVersion, nowMs, sequence);
}
SignalResult SnapshotReceiver::select(const std::vector<SignalTarget>& targets,
                                      const std::string& gameVersion, std::uint64_t nowMs) const {
    return impl_->receiver.select(targets, gameVersion, nowMs);
}
} // namespace signal_test
} // namespace lane
