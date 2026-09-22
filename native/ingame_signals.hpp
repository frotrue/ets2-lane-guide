#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lane {

struct SignalPosition { double x, y, z; };
struct SignalTarget { int id; std::vector<SignalPosition> positions; };
struct SignalResult {
    bool available = false;
    // 1 = yellow toward red; 2 = red; 4 = RED + yellow; 8 = green.
    // State 4 retains its red/stop meaning. Zero/off and blinking are unavailable.
    int rawState = 0;
    int remainingSeconds = 0; // Until this state changes, not necessarily until green.
};

// Call update from one thread at 100-200 ms intervals. No external process,
// game-memory scan, input mapping, persistent mapping handle, or worker thread.
class SignalReader {
public:
    SignalReader();
    ~SignalReader();
    SignalReader(const SignalReader&) = delete;
    SignalReader& operator=(const SignalReader&) = delete;
    void clear();
    SignalResult update(const std::vector<SignalTarget>& targets, bool active,
                        const std::string& gameVersion);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Explicit deterministic test seam for the same receiver used by SignalReader.
// This API does not open mappings or attach to a game. Packed bytes are the
// published ETS2LA output records, not addresses in the game's memory.
namespace signal_test {
class SnapshotReceiver {
public:
    SnapshotReceiver();
    ~SnapshotReceiver();
    SnapshotReceiver(SnapshotReceiver&&) noexcept;
    SnapshotReceiver& operator=(SnapshotReceiver&&) noexcept;
    SnapshotReceiver(const SnapshotReceiver&) = delete;
    SnapshotReceiver& operator=(const SnapshotReceiver&) = delete;
    void clear();
    bool ingestSnapshot(const std::uint8_t* data, std::size_t size, int pluginVersion,
                        std::uint64_t nowMs, std::uint64_t sequence);
    SignalResult select(const std::vector<SignalTarget>& targets,
                        const std::string& gameVersion, std::uint64_t nowMs) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace signal_test
} // namespace lane
