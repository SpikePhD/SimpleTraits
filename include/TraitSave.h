#pragma once

#include "TraitRules.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Dependency-free codec for Simple Traits' cosave record.
//
// Record 'TRTS' v2, little-endian:
//   uint32 allocation[6]              indexed by TraitRules::Trait
//   uint32 skillPointsGranted         legacy: SAL skill points granted by the old
//                                     Intelligence (before it became an XP
//                                     multiplier); kept and written back unchanged
//   uint32 appliedCount               0..kMaxAppliedEntries
//   { uint32 actorValueId, float32 amount } x appliedCount
// v1 is the same without skillPointsGranted and is still read (granted = 0).
//
// Applied entries are keyed by RE::ActorValue id, so later bonuses (for
// example CriticalChance) can be added to the whitelist without a new layout.
namespace ST::TraitSave {

    // Four-character codes, spelled out because multi-character literals are
    // implementation-defined: same values as MSVC's 'SMTR' and 'TRTS'.
    constexpr std::uint32_t FourCC(char a, char b, char c, char d) noexcept
    {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8) |
               static_cast<std::uint32_t>(static_cast<unsigned char>(d));
    }
    inline constexpr std::uint32_t kUniqueID = FourCC('S', 'M', 'T', 'R');
    inline constexpr std::uint32_t kRecordType = FourCC('T', 'R', 'T', 'S');
    inline constexpr std::uint32_t kVersion = 2;
    inline constexpr std::uint32_t kVersion1 = 1;

    inline constexpr std::size_t kMaxAppliedEntries = 8;
    inline constexpr std::size_t kHeaderSizeV1 = TraitRules::kTraitCount * 4 + 4;
    inline constexpr std::size_t kHeaderSize = kHeaderSizeV1 + 4;
    inline constexpr std::size_t kEntrySize = 8;
    inline constexpr std::size_t kMaxRecordSize = kHeaderSize + kMaxAppliedEntries * kEntrySize;

    struct State {
        TraitRules::Allocation     allocation{};
        TraitRules::AppliedBonuses applied{};
        std::uint32_t              skillPointsGranted{ 0 };

        friend bool operator==(const State&, const State&) = default;
    };

    enum class DecodeStatus {
        kSuccess,
        kUnsupportedVersion,
        kInvalidLength,
        kInvalidData  // over-cap allocation, unknown or duplicate actor value, non-finite amount
    };

    struct DecodeResult {
        DecodeStatus status{ DecodeStatus::kInvalidData };
        State        state{};

        [[nodiscard]] bool Succeeded() const noexcept { return status == DecodeStatus::kSuccess; }
    };

    [[nodiscard]] std::string_view StatusName(DecodeStatus status) noexcept;

    // Writes the current version with every known bonus, so the record
    // length is fixed per version.
    [[nodiscard]] std::vector<std::byte> Encode(const State& state);

    [[nodiscard]] DecodeResult Decode(std::uint32_t version, std::span<const std::byte> data) noexcept;

    // The first valid record wins; returns false when `accepted` was already set.
    [[nodiscard]] bool AdoptFirstValid(std::optional<State>& accepted, const DecodeResult& decoded) noexcept;
}
