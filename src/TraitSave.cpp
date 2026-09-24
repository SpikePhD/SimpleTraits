#include "TraitSave.h"

#include <bit>
#include <cmath>

namespace ST::TraitSave {
    namespace {
        void AppendU32(std::vector<std::byte>& bytes, std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8) {
                bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
            }
        }

        std::uint32_t ReadU32(std::span<const std::byte> data, std::size_t offset) noexcept
        {
            std::uint32_t value = 0;
            for (std::size_t i = 0; i < 4; ++i) {
                value |= static_cast<std::uint32_t>(data[offset + i]) << (8 * i);
            }
            return value;
        }
    }

    std::string_view StatusName(DecodeStatus status) noexcept
    {
        switch (status) {
            case DecodeStatus::kSuccess: return "success";
            case DecodeStatus::kUnsupportedVersion: return "unsupported-version";
            case DecodeStatus::kInvalidLength: return "invalid-length";
            case DecodeStatus::kInvalidData: return "invalid-data";
        }
        return "unknown";
    }

    std::vector<std::byte> Encode(const State& state)
    {
        std::vector<std::byte> bytes;
        bytes.reserve(kHeaderSize + TraitRules::kBonusCount * kEntrySize);
        for (const auto points : state.allocation) {
            AppendU32(bytes, points);
        }
        AppendU32(bytes, state.skillPointsGranted);
        AppendU32(bytes, static_cast<std::uint32_t>(TraitRules::kBonusCount));
        for (std::size_t i = 0; i < TraitRules::kBonusCount; ++i) {
            AppendU32(bytes, TraitRules::BonusActorValueId(static_cast<TraitRules::Bonus>(i)));
            AppendU32(bytes, std::bit_cast<std::uint32_t>(state.applied[i]));
        }
        return bytes;
    }

    DecodeResult Decode(std::uint32_t version, std::span<const std::byte> data) noexcept
    {
        if (version != kVersion && version != kVersion1) {
            return { DecodeStatus::kUnsupportedVersion };
        }
        const std::size_t headerSize = version == kVersion1 ? kHeaderSizeV1 : kHeaderSize;
        if (data.size() < headerSize || data.size() > kMaxRecordSize) {
            return { DecodeStatus::kInvalidLength };
        }

        State state;
        std::size_t offset = 0;
        for (auto& points : state.allocation) {
            points = ReadU32(data, offset);
            offset += 4;
            if (points > TraitRules::kMaxPointsPerTrait) {
                return { DecodeStatus::kInvalidData };
            }
        }

        if (version != kVersion1) {
            state.skillPointsGranted = ReadU32(data, offset);
            offset += 4;
        }

        const auto count = ReadU32(data, offset);
        offset += 4;
        if (count > kMaxAppliedEntries || data.size() != headerSize + count * kEntrySize) {
            return { DecodeStatus::kInvalidLength };
        }

        std::array<bool, TraitRules::kBonusCount> seen{};
        for (std::uint32_t entry = 0; entry < count; ++entry) {
            const auto id = ReadU32(data, offset);
            const auto amount = std::bit_cast<float>(ReadU32(data, offset + 4));
            offset += kEntrySize;
            const auto bonus = TraitRules::BonusFromActorValueId(id);
            if (!bonus || !std::isfinite(amount)) {
                return { DecodeStatus::kInvalidData };
            }
            const auto index = static_cast<std::size_t>(*bonus);
            if (seen[index]) {
                return { DecodeStatus::kInvalidData };
            }
            seen[index] = true;
            state.applied[index] = amount;
        }
        return { DecodeStatus::kSuccess, state };
    }

    bool AdoptFirstValid(std::optional<State>& accepted, const DecodeResult& decoded) noexcept
    {
        if (!decoded.Succeeded() || accepted) {
            return false;
        }
        accepted = decoded.state;
        return true;
    }
}
