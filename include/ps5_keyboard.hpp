#pragma once

/*
 * Independently authored interoperability declarations adapted from
 * blackbearreloaded/ps5-native-gamepad-input-research (GPL-3.0-or-later).
 * No vendor SDK header or source code is included.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ps5::keyboard {

inline constexpr std::int32_t kTypeStandard = 0;
inline constexpr std::uint16_t kSysmoduleId = 0x0106;
inline constexpr std::size_t kSampleSize = 96;
inline constexpr std::size_t kMaxSamples = 16;
inline constexpr std::size_t kMaxKeys = 16;
inline constexpr std::size_t kMaxOpenHandles = 12;

using LedMask = std::uint32_t;
using ModifierMask = std::uint32_t;

inline constexpr LedMask kLedNumLock = 1U << 0U;
inline constexpr LedMask kLedCapsLock = 1U << 1U;
inline constexpr LedMask kLedScrollLock = 1U << 2U;

inline constexpr ModifierMask kModifierLeftControl = 1U << 0U;
inline constexpr ModifierMask kModifierLeftShift = 1U << 1U;
inline constexpr ModifierMask kModifierLeftAlt = 1U << 2U;
inline constexpr ModifierMask kModifierLeftGui = 1U << 3U;
inline constexpr ModifierMask kModifierRightControl = 1U << 4U;
inline constexpr ModifierMask kModifierRightShift = 1U << 5U;
inline constexpr ModifierMask kModifierRightAlt = 1U << 6U;
inline constexpr ModifierMask kModifierRightGui = 1U << 7U;

struct OpenParameters {
    std::array<std::uint8_t, 8> reserved;
};

struct Data {
    std::uint64_t timestamp_us;
    std::uint8_t intercepted;
    std::array<std::uint8_t, 7> reserved0;
    std::uint8_t connected;
    std::array<std::uint8_t, 3> reserved1;
    std::int32_t length;
    LedMask leds;
    ModifierMask modifiers;
    std::array<std::uint16_t, kMaxKeys> keycodes;
    std::array<std::uint8_t, 32> reserved2;
};

[[nodiscard]] constexpr bool is_usable(const Data& sample) noexcept
{
    return sample.connected != 0 && sample.intercepted == 0;
}

[[nodiscard]] constexpr bool contains_usage(
    const Data& sample, const std::uint16_t usage) noexcept
{
    if (usage == 0 || !is_usable(sample)) {
        return false;
    }
    for (const auto keycode : sample.keycodes) {
        if (keycode == usage) {
            return true;
        }
    }
    return false;
}

static_assert(sizeof(OpenParameters) == 8);
static_assert(sizeof(Data) == kSampleSize);
static_assert(offsetof(Data, intercepted) == 0x08);
static_assert(offsetof(Data, connected) == 0x10);
static_assert(offsetof(Data, length) == 0x14);
static_assert(offsetof(Data, leds) == 0x18);
static_assert(offsetof(Data, modifiers) == 0x1c);
static_assert(offsetof(Data, keycodes) == 0x20);
static_assert(std::is_standard_layout_v<Data>);
static_assert(std::is_trivially_copyable_v<Data>);

}  // namespace ps5::keyboard

extern "C" {

std::int32_t sceKeyboardInit();
std::int32_t sceKeyboardOpen(
    std::int32_t user_id, std::int32_t type, std::int32_t index,
    const ps5::keyboard::OpenParameters* parameters);
std::int32_t sceKeyboardClose(std::int32_t handle);
std::int32_t sceKeyboardGetHandle(
    std::int32_t user_id, std::int32_t type, std::int32_t index);
std::int32_t sceKeyboardReadState(
    std::int32_t handle, ps5::keyboard::Data* data);
std::int32_t sceKeyboardRead(
    std::int32_t handle, ps5::keyboard::Data* data,
    std::int32_t capacity);

}  // extern "C"
