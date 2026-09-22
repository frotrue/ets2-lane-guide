#pragma once
#include <cstdint>
#include <cstddef>

namespace lane {
// Local read-only route output. Sequence is odd while the producer writes.
// All distances/times come from the game's physical route, never from a new route search.
constexpr std::uint32_t RouteMagic=0x3150474c; // LGP1
constexpr std::uint32_t RouteProducer=1610;
constexpr std::size_t RouteCapacity=6000;
enum class RouteStatus:std::uint32_t { NoRoute=0,Ready=1,Unavailable=2 };
struct RouteRecord { std::uint64_t uid;float distance,time; };
struct RoutePacket {
 std::uint32_t sequence,magic,producer,count;
 std::uint64_t publishedAt;
 RouteStatus status;
 std::uint32_t reserved;
 RouteRecord records[RouteCapacity];
};
static_assert(sizeof(RouteRecord)==16&&offsetof(RoutePacket,records)==32&&sizeof(RoutePacket)==96032);
}
