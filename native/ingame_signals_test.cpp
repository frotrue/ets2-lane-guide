#include "ingame_signals.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Corresponds to tests/traffic-signal-receiver.test.cjs. These fixtures are
// injected directly; this executable neither creates real producer mappings
// nor starts a game, child process, DLL installer, or the desktop application.
namespace {
using lane::SignalTarget;
using lane::signal_test::SnapshotReceiver;
using Bytes = std::array<std::uint8_t, 1920>;
int assertions = 0, suites = 0;

void expect(bool condition, const std::string& message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
void suite(const char* name, const std::function<void()>& run) {
    run(); ++suites; std::cout << "PASS: " << name << '\n';
}

#pragma pack(push, 1)
struct PackedSignal {
    float x = 100, y = 4, z = 200;
    std::int16_t cx = 0, cz = 0;
    float qw = 1, qx = 0, qy = 0, qz = 0;
    std::int32_t type = 1;
    float remaining = 10;
    std::int32_t state = 2, id = 3;
};
#pragma pack(pop)
static_assert(sizeof(PackedSignal) == 48);

Bytes snapshot(const std::vector<PackedSignal>& signals) {
    Bytes bytes{};
    if (signals.size() > 40) throw std::runtime_error("Fixture overflow");
    for (std::size_t i = 0; i < signals.size(); ++i)
        std::memcpy(bytes.data() + i * 48, &signals[i], sizeof(PackedSignal));
    return bytes;
}
bool ingest(SnapshotReceiver& receiver, const std::vector<PackedSignal>& signals,
            std::uint64_t now = 1000, std::uint64_t sequence = 1, int version = 1610) {
    const auto bytes = snapshot(signals);
    return receiver.ingestSnapshot(bytes.data(), bytes.size(), version, now, sequence);
}
std::vector<SignalTarget> targets() { return {{3, {{100, 4, 200}}}}; }
lane::SignalResult select(const SnapshotReceiver& receiver, std::uint64_t now = 1200) {
    return receiver.select(targets(), "1.61.1.0", now);
}
SnapshotReceiver ticking(std::vector<PackedSignal> signals = {PackedSignal{}}) {
    SnapshotReceiver receiver;
    expect(ingest(receiver, signals), "Initial fixture rejected");
    for (auto& signal : signals) signal.remaining -= 0.2f;
    expect(ingest(receiver, signals, 1200, 2), "Ticking fixture rejected");
    return receiver;
}
} // namespace

int main() {
    try {
        suite("packed record, signed world cells, malformed snapshots", [] {
            PackedSignal signal; signal.cx = -2; signal.cz = 3;
            auto receiver = ticking({signal});
            expect(receiver.select({{3, {{-924, 4, 1736}}}}, "1.61.1.0", 1200).available, "Signed cell conversion");
            expect(!select(receiver).available, "Local coordinates must not match world positions");
            const auto bytes = snapshot({signal});
            expect(!receiver.ingestSnapshot(bytes.data(), bytes.size() - 1, 1610, 1300, 3), "Truncated snapshot accepted");
            expect(!receiver.ingestSnapshot(nullptr, bytes.size(), 1610, 1400, 4), "Null snapshot accepted");
            for (int variant = 0; variant < 8; ++variant) {
                PackedSignal bad;
                switch (variant) {
                    case 0: bad.x = std::numeric_limits<float>::quiet_NaN(); break;
                    case 1: bad.remaining = -1; break;
                    case 2: bad.remaining = std::numeric_limits<float>::infinity(); break;
                    case 3: bad.state = 17; break;
                    case 4: bad.type = 7; break;
                    case 5: bad.id = -1; break;
                    case 6: bad.qw = 0; break;
                    case 7: bad.qx = std::numeric_limits<float>::quiet_NaN(); break;
                }
                expect(!ingest(receiver, {bad}), "Malformed signal accepted");
            }
        });
        suite("initial sample withheld until timer progression", [] {
            SnapshotReceiver receiver;
            PackedSignal signal;
            expect(ingest(receiver, {signal}), "Initial sample");
            expect(!select(receiver, 1000).available, "Unverified first sample visible");
            signal.remaining = 9.8f;
            expect(ingest(receiver, {signal}, 1200, 2), "Next sample");
            const auto result = select(receiver);
            expect(result.available && result.rawState == 2 && result.remainingSeconds == 10, "Live countdown mismatch");
        });
        suite("route ID and 3D locator required, not nearest light", [] {
            PackedSignal otherId; otherId.id = 8; otherId.x = 99;
            PackedSignal remote; remote.x = 500; remote.state = 8;
            auto receiver = ticking({otherId, PackedSignal{}, remote});
            expect(select(receiver).rawState == 2, "Wrong nearby signal selected");
            expect(!receiver.select({{9, {{100, 4, 200}}}}, "1.61.1.0", 1200).available, "Wrong ID matched");
            expect(!receiver.select({{3, {{100, 12, 200}}}}, "1.61.1.0", 1200).available, "Wrong elevation matched");
            expect(!receiver.select({{3, {{110, 4, 200}}}}, "1.61.1.0", 1200).available, "Remote locator matched");
            expect(!receiver.select({}, "1.61.1.0", 1200).available, "Empty targets visible");
            expect(!receiver.select({{3, {}}}, "1.61.1.0", 1200).available, "Empty positions visible");
            expect(!receiver.select({{-1, {{100, 4, 200}}}}, "1.61.1.0", 1200).available, "Invalid target ID");
            expect(!receiver.select({{3, {{std::numeric_limits<double>::quiet_NaN(), 4, 200}}}}, "1.61.1.0", 1200).available, "Invalid target position");
        });
        suite("every target group and alternative locator", [] {
            PackedSignal other; other.id = 4; other.x = 120;
            auto receiver = ticking({PackedSignal{}, other});
            auto all = targets(); all[0].positions.insert(all[0].positions.begin(), {90, 4, 200});
            all.push_back({4, {{120, 4, 200}}});
            expect(receiver.select(all, "1.61.1.0", 1200).available, "Alternative locator not accepted");
            all.push_back({5, {{100, 4, 200}}});
            expect(!receiver.select(all, "1.61.1.0", 1200).available, "Missing target group accepted");
        });
        suite("all matched colors and full time range must agree", [] {
            PackedSignal other; other.x = 101; other.state = 8;
            expect(!select(ticking({PackedSignal{}, other})).available, "Contradictory phase visible");
            other.state = 2; other.remaining = 8;
            expect(!select(ticking({PackedSignal{}, other})).available, "Contradictory timers visible");
            other.remaining = 9.8f;
            PackedSignal third; third.x = 99; third.remaining = 10.2f;
            expect(!select(ticking({PackedSignal{}, other, third})).available, "Full timer-range conflict missed");
        });
        suite("identical identities may repeat only with agreement", [] {
            PackedSignal second;
            expect(select(ticking({PackedSignal{}, second})).available, "Consistent duplicate hidden");
            second.remaining = 9.9f;
            expect(select(ticking({PackedSignal{}, second})).available, "Within-tolerance duplicate hidden");
            second.remaining = 7;
            expect(!select(ticking({PackedSignal{}, second})).available, "Timer-conflicting duplicate visible");
            second.remaining = 10; second.state = 8;
            expect(!select(ticking({PackedSignal{}, second})).available, "State-conflicting duplicate visible");
        });
        suite("strict state enum, including red-yellow, off, blinking, gates", [] {
            for (const int state : {1, 2, 4, 8}) {
                PackedSignal signal; signal.state = state;
                const auto result = select(ticking({signal}));
                expect(result.available && result.rawState == state, "Active state lost or remapped");
            }
            for (const int state : {0, 32}) {
                PackedSignal signal; signal.state = state;
                expect(!select(ticking({signal})).available, "Off or blinking state visible");
            }
            PackedSignal gate; gate.type = 2;
            expect(!select(ticking({gate})).available, "Gate displayed as traffic light");
        });
        suite("750ms freshness and 1000ms progress watchdog", [] {
            auto receiver = ticking();
            expect(select(receiver, 1950).available, "Freshness boundary rejected");
            expect(!select(receiver, 1951).available, "Stale snapshot visible");
            PackedSignal frozen; frozen.remaining = 9.8f;
            for (std::uint64_t i = 1; i <= 6; ++i)
                expect(ingest(receiver, {frozen}, 1200 + i * 200, i + 2), "Frozen read rejected prematurely");
            expect(!select(receiver, 2400).available, "Frozen producer kept alive by fresh reads");
            frozen.remaining = 9.6f;
            expect(ingest(receiver, {frozen}, 2600, 9), "Recovered timer rejected");
            expect(select(receiver, 2600).available, "Recovered timer invisible");
            receiver.clear();
            expect(!select(receiver, 2600).available, "Clear retained live data");
        });
        suite("unsupported versions and bad data clear previous observations", [] {
            auto receiver = ticking();
            expect(!receiver.select(targets(), "1.62.0.0", 1200).available, "Unsupported map version visible");
            expect(!receiver.select(targets(), "1.610.0", 1200).available, "Version prefix collision");
            expect(!ingest(receiver, {PackedSignal{}}, 1400, 3, 1600), "Unsupported plugin version accepted");
            expect(!select(receiver, 1400).available, "Unsupported version retained previous value");
            receiver = ticking();
            auto bytes = snapshot({PackedSignal{}});
            expect(!receiver.ingestSnapshot(bytes.data(), 10, 1610, 1400, 3), "Malformed snapshot accepted");
            expect(!select(receiver, 1400).available, "Malformed snapshot retained previous value");
        });
        suite("timer restart, backwards clock, duplicate sequence invalidate progress", [] {
            for (int variant = 0; variant < 3; ++variant) {
                auto receiver = ticking();
                PackedSignal signal; signal.remaining = variant == 0 ? 15.0f : 9.6f;
                const std::uint64_t now = variant == 2 ? 1100 : 1400;
                ingest(receiver, {signal}, now, variant == 1 ? 1 : 3);
                expect(!select(receiver, now).available, "Reset or stale sequence remained live");
            }
        });
        suite("normal transitions corroborate progress; skipped phases do not", [] {
            SnapshotReceiver receiver;
            PackedSignal signal; signal.state = 2;
            ingest(receiver, {signal});
            signal.state = 4; signal.remaining = 2;
            ingest(receiver, {signal}, 1200, 2);
            expect(select(receiver).available && select(receiver).rawState == 4, "Red-yellow transition lost");
            signal.state = 8; signal.remaining = 20;
            ingest(receiver, {signal}, 1400, 3);
            expect(select(receiver, 1400).available && select(receiver, 1400).rawState == 8, "Green transition lost");
            signal.state = 2; signal.remaining = 25;
            ingest(receiver, {signal}, 1600, 4);
            expect(!select(receiver, 1600).available, "Skipped phase trusted immediately");
        });
        suite("centimetre identities preserve JavaScript decimal tie rounding", [] {
            SnapshotReceiver receiver;
            PackedSignal signal; signal.y = 0.125f;
            expect(ingest(receiver, {signal}), "Decimal-tie fixture rejected");
            signal.y = 0.126f; signal.remaining = 9.8f;
            expect(ingest(receiver, {signal}, 1200, 2), "Decimal-tie next fixture rejected");
            expect(receiver.select({{3, {{100, 0.125, 200}}}}, "1.61.1.0", 1200).available,
                   "JS .125 and .126 both have the .13 identity");
        });
        suite("inactive reader never produces a signal", [] {
            lane::SignalReader reader;
            expect(!reader.update(targets(), false, "1.61.1.0").available, "Inactive reader visible");
            reader.clear();
            expect(!reader.update(targets(), true, "1.62.0.0").available, "Unsupported reader version visible");
        });
        std::cout << "PASS: " << suites << " suites, " << assertions << " assertions. No game or external process used.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
