#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

namespace dg {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;

static_assert(sizeof(f32) == 4 && sizeof(f64) == 8);
// Serialized data, snapshots and state hashes assume little-endian on every target platform.
static_assert(std::endian::native == std::endian::little, "Detergine requires a little-endian target");

} // namespace dg
