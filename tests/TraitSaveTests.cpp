#include "TraitSave.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace {
    int failures = 0;

    void Check(bool condition, std::string_view message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    using namespace ST::TraitSave;
    using ST::TraitRules::Allocation;

    void AppendU32(std::vector<std::byte>& bytes, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
        }
    }

    void AppendFloat(std::vector<std::byte>& bytes, float value)
    {
        AppendU32(bytes, std::bit_cast<std::uint32_t>(value));
    }

    // Hand-built record: allocation, then (id, amount) pairs.
    std::vector<std::byte> Record(const Allocation& allocation,
        const std::vector<std::pair<std::uint32_t, float>>& entries)
    {
        std::vector<std::byte> bytes;
        for (const auto points : allocation) {
            AppendU32(bytes, points);
        }
        AppendU32(bytes, static_cast<std::uint32_t>(entries.size()));
        for (const auto& [id, amount] : entries) {
            AppendU32(bytes, id);
            AppendFloat(bytes, amount);
        }
        return bytes;
    }

    void TestIdentifiers()
    {
        // Same values MSVC gives the multi-character literals 'SMTR' and 'TRTS'.
        Check(kUniqueID == 0x534D5452u, "unique ID is 'SMTR'");
        Check(kRecordType == 0x54525453u, "record type is 'TRTS'");
    }

    void TestRoundTrip()
    {
        const State state{ { 3, 1, 0, 2, 5, 0 }, { 15.0f, 5.0f, 25.0f } };
        const auto bytes = Encode(state);
        Check(bytes.size() == kHeaderSize + 3 * kEntrySize, "encoded length is fixed for v1");
        const auto decoded = Decode(kVersion, bytes);
        Check(decoded.Succeeded(), "round trip decodes");
        Check(decoded.state == state, "round trip preserves state");

        const auto empty = Decode(kVersion, Encode(State{}));
        Check(empty.Succeeded() && empty.state == State{}, "empty state round trips");

        // Little-endian layout: first allocation, then the Stamina entry id (26).
        Check(bytes[0] == std::byte{ 3 } && bytes[1] == std::byte{ 0 }, "allocation is little-endian");
        Check(bytes[kHeaderSize] == std::byte{ 26 }, "first applied entry is Stamina (26)");
    }

    void TestFlexibleEntries()
    {
        const Allocation allocation{ 1, 1, 1, 1, 1, 1 };
        const auto reordered = Decode(kVersion, Record(allocation, { { 25, 5.0f }, { 24, 10.0f } }));
        Check(reordered.Succeeded(), "entries in any order, subset allowed");
        Check(reordered.state.applied[0] == 0.0f && reordered.state.applied[1] == 10.0f &&
                  reordered.state.applied[2] == 5.0f,
            "missing entry defaults to 0, others map by actor value id");
        Check(Decode(kVersion, Record(allocation, {})).Succeeded(), "zero entries allowed");
    }

    void TestRejections()
    {
        const Allocation allocation{};
        const auto valid = Encode(State{});

        Check(Decode(0, valid).status == DecodeStatus::kUnsupportedVersion, "version 0 rejected");
        Check(Decode(2, valid).status == DecodeStatus::kUnsupportedVersion, "future version rejected");

        auto truncated = valid;
        truncated.pop_back();
        Check(Decode(kVersion, truncated).status == DecodeStatus::kInvalidLength, "truncated record rejected");
        auto trailing = valid;
        trailing.push_back(std::byte{ 0 });
        Check(Decode(kVersion, trailing).status == DecodeStatus::kInvalidLength, "trailing byte rejected");
        Check(Decode(kVersion, std::vector<std::byte>(kHeaderSize - 1)).status == DecodeStatus::kInvalidLength,
            "short header rejected");
        Check(Decode(kVersion, std::vector<std::byte>(kMaxRecordSize + 1)).status == DecodeStatus::kInvalidLength,
            "oversized record rejected");

        auto badCount = Record(allocation, {});
        badCount[kHeaderSize - 4] = std::byte{ 9 };  // count 9 > kMaxAppliedEntries
        Check(Decode(kVersion, badCount).status == DecodeStatus::kInvalidLength, "count above maximum rejected");
        auto countMismatch = Record(allocation, { { 24, 1.0f } });
        countMismatch[kHeaderSize - 4] = std::byte{ 2 };
        Check(Decode(kVersion, countMismatch).status == DecodeStatus::kInvalidLength, "count/length mismatch rejected");

        Allocation overCap{};
        overCap[2] = ST::TraitRules::kMaxPointsPerTrait + 1;
        Check(Decode(kVersion, Record(overCap, {})).status == DecodeStatus::kInvalidData, "over-cap allocation rejected");
        Allocation atCap{};
        atCap[2] = ST::TraitRules::kMaxPointsPerTrait;
        Check(Decode(kVersion, Record(atCap, {})).Succeeded(), "allocation at cap accepted");

        Check(Decode(kVersion, Record(allocation, { { 33, 1.0f } })).status == DecodeStatus::kInvalidData,
            "actor value outside the whitelist rejected");
        Check(Decode(kVersion, Record(allocation, { { 24, 1.0f }, { 24, 2.0f } })).status == DecodeStatus::kInvalidData,
            "duplicate actor value rejected");
        Check(Decode(kVersion, Record(allocation, { { 24, std::numeric_limits<float>::quiet_NaN() } })).status ==
                  DecodeStatus::kInvalidData,
            "NaN amount rejected");
        Check(Decode(kVersion, Record(allocation, { { 25, std::numeric_limits<float>::infinity() } })).status ==
                  DecodeStatus::kInvalidData,
            "infinite amount rejected");
        Check(Decode(kVersion, Record(allocation, { { 26, -5.0f } })).Succeeded(),
            "negative applied amount is valid (a lowered setting can leave it below zero)");
    }

    void TestAdoption()
    {
        std::optional<State> accepted;
        const DecodeResult first{ DecodeStatus::kSuccess, State{ { 1, 0, 0, 0, 0, 0 }, {} } };
        const DecodeResult second{ DecodeStatus::kSuccess, State{ { 2, 0, 0, 0, 0, 0 }, {} } };
        const DecodeResult failed{ DecodeStatus::kInvalidData, {} };
        Check(!AdoptFirstValid(accepted, failed) && !accepted, "failed record not adopted");
        Check(AdoptFirstValid(accepted, first) && accepted && accepted->allocation[0] == 1, "first valid adopted");
        Check(!AdoptFirstValid(accepted, second) && accepted->allocation[0] == 1, "later valid record ignored");
    }
}

int main()
{
    TestIdentifiers();
    TestRoundTrip();
    TestFlexibleEntries();
    TestRejections();
    TestAdoption();
    if (failures == 0) {
        std::cout << "All trait-save tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
