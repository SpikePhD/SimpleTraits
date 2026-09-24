#include "PCH.h"
#include "TraitState.h"

#include "Config.h"
#include "TraitSave.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <vector>

namespace ST::TraitState {
    namespace {
        using TraitRules::Bonus;

        static_assert(TraitRules::kBonusCount == 4);
        static_assert(static_cast<std::uint32_t>(RE::ActorValue::kCriticalChance) == 33);
        static_assert(static_cast<std::uint32_t>(RE::ActorValue::kHealth) == 24);
        static_assert(static_cast<std::uint32_t>(RE::ActorValue::kMagicka) == 25);
        static_assert(static_cast<std::uint32_t>(RE::ActorValue::kStamina) == 26);

        std::mutex                s_mutex;
        TraitSave::State          s_state;
        bool                      s_gameActive{ false };
        // Copy of Config::traits, so snapshots from the render thread never
        // read the Config globals the settings page rewrites.
        TraitRules::TraitSettings s_settings{};

        // Charisma: fBarterMin/fBarterMax are global game settings, not saved
        // per character. The originals are captured once on kDataLoaded; ST
        // writes ApplyCharisma(originals) and remembers what it wrote, so a
        // change by another mod is detected and adopted as the new original.
        std::optional<TraitRules::BarterSettings> s_barterOriginal;
        std::optional<TraitRules::BarterSettings> s_barterWritten;

        RE::Setting* GameSetting(const char* name)
        {
            auto* settings = RE::GameSettingCollection::GetSingleton();
            auto* setting = settings ? settings->GetSetting(name) : nullptr;
            return setting && setting->GetType() == RE::Setting::Type::kFloat ? setting : nullptr;
        }

        std::optional<TraitRules::BarterSettings> ReadBarter()
        {
            auto* min = GameSetting("fBarterMin");
            auto* max = GameSetting("fBarterMax");
            if (!min || !max) {
                return std::nullopt;
            }
            return TraitRules::BarterSettings{ min->GetFloat(), max->GetFloat() };
        }

        void WriteBarter(const TraitRules::BarterSettings& values)
        {
            if (auto* min = GameSetting("fBarterMin")) min->data.f = values.barterMin;
            if (auto* max = GameSetting("fBarterMax")) max->data.f = values.barterMax;
            s_barterWritten = values;
        }

        float CriticalChancePerPercent()
        {
            auto* setting = GameSetting("fWeaponConditionCriticalChanceMult");
            return TraitRules::CriticalChancePerPercent(setting ? setting->GetFloat() : 0.0f);
        }

        // Caller holds s_mutex; main thread.
        void ReconcileBarterLocked(std::string_view reason, bool& changed)
        {
            if (!s_barterOriginal) {
                return;
            }
            const auto current = ReadBarter();
            if (!current) {
                return;
            }
            if (s_barterWritten && (current->barterMin != s_barterWritten->barterMin ||
                                       current->barterMax != s_barterWritten->barterMax)) {
                logger::warn("[ST] Charisma: fBarterMin/fBarterMax changed by something else ({:.3f}/{:.3f}); "
                             "using them as the new originals.", current->barterMin, current->barterMax);
                s_barterOriginal = current;
            }
            const auto charisma = s_state.allocation[static_cast<std::size_t>(TraitRules::Trait::kCharisma)];
            const auto desired = TraitRules::ApplyCharisma(*s_barterOriginal, charisma, s_settings.charismaPriceImprovement);
            if (desired.barterMin == current->barterMin && desired.barterMax == current->barterMax) {
                s_barterWritten = desired;
                return;
            }
            WriteBarter(desired);
            changed = true;
            logger::info("[ST] Reconcile ({}): Charisma {} points: fBarterMin {:.3f} -> {:.3f}, fBarterMax {:.3f} -> {:.3f} "
                         "(originals {:.3f}/{:.3f}; price factor x{:.3f}).",
                reason, charisma, current->barterMin, desired.barterMin, current->barterMax, desired.barterMax,
                s_barterOriginal->barterMin, s_barterOriginal->barterMax,
                s_barterOriginal->barterMax > 0.0f ? desired.barterMax / s_barterOriginal->barterMax : 1.0f);
        }

        RE::ActorValue ToActorValue(Bonus bonus)
        {
            return static_cast<RE::ActorValue>(TraitRules::BonusActorValueId(bonus));
        }

        std::int64_t PlayerLevel()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player ? player->GetLevel() : 0;
        }

        std::string DescribeAllocation(const TraitRules::Allocation& allocation)
        {
            std::string text;
            for (std::size_t i = 0; i < TraitRules::kTraitCount; ++i) {
                text += std::format("{}{}={}", i ? ", " : "", TraitRules::TraitKey(static_cast<TraitRules::Trait>(i)),
                    allocation[i]);
            }
            return text;
        }

        std::string DescribeApplied(const TraitRules::AppliedBonuses& applied)
        {
            std::string text;
            for (std::size_t i = 0; i < TraitRules::kBonusCount; ++i) {
                text += std::format("{}{}={:.2f}", i ? ", " : "", TraitRules::BonusName(static_cast<Bonus>(i)),
                    applied[i]);
            }
            return text;
        }

        // One line with everything needed to follow a test from the log alone.
        // Caller holds s_mutex; main thread (reads actor values).
        void LogStateLocked(std::string_view reason)
        {
            const auto level = PlayerLevel();
            const auto& points = s_settings.points;
            std::string values = "player unavailable";
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (auto* avo = player ? player->AsActorValueOwner() : nullptr) {
                values.clear();
                for (std::size_t i = 0; i < TraitRules::kBonusCount; ++i) {
                    const auto bonus = static_cast<Bonus>(i);
                    const auto av = ToActorValue(bonus);
                    values += std::format("{}{} current={:.1f} permanent={:.1f} base={:.1f}", i ? "; " : "",
                        TraitRules::BonusName(bonus), avo->GetActorValue(av), avo->GetPermanentActorValue(av),
                        avo->GetBaseActorValue(av));
                }
            }
            logger::info("[ST] State ({}): level {}, earned {}, spent {}, unspent {}; points {}; ST applied {}; "
                         "skill points granted {}; {}.",
                reason, level, TraitRules::EarnedPoints(level, points), TraitRules::SpentPoints(s_state.allocation),
                TraitRules::UnspentPoints(level, points, s_state.allocation), DescribeAllocation(s_state.allocation),
                DescribeApplied(s_state.applied), s_state.skillPointsGranted, values);
        }

        // Caller holds s_mutex. With quietWhenUnchanged, a reconcile that
        // changes nothing logs nothing at info level (frequent triggers).
        void ReconcileLocked(std::string_view reason, bool quietWhenUnchanged = false)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* avo = player ? player->AsActorValueOwner() : nullptr;
            if (!avo) {
                logger::warn("[ST] Reconcile ({}): player unavailable; nothing applied.", reason);
                return;
            }

            // Bonuses are a percentage of the BASE value only, so the plugin's
            // own permanent modifier never feeds back into its target.
            TraitRules::BaseValues bases{};
            for (std::size_t i = 0; i < TraitRules::kBonusCount; ++i) {
                bases[i] = avo->GetBaseActorValue(ToActorValue(static_cast<Bonus>(i)));
            }
            const auto plans = TraitRules::PlanReconcile(
                s_state.allocation, s_settings, bases, s_state.applied, CriticalChancePerPercent());
            bool changed = false;
            ReconcileBarterLocked(reason, changed);
            for (std::size_t i = 0; i < TraitRules::kBonusCount; ++i) {
                const auto bonus = static_cast<Bonus>(i);
                const auto& plan = plans[i];
                if (!plan.valid) {
                    logger::error("[ST] Reconcile ({}): {} base {:.2f} or recorded bonus {:.2f} unusable; left untouched.",
                        reason, TraitRules::BonusName(bonus), bases[i], s_state.applied[i]);
                    continue;
                }
                if (std::fabs(plan.delta) < 0.001f) {
                    logger::debug("[ST] Reconcile ({}): {} unchanged at {:.2f}.",
                        reason, TraitRules::BonusName(bonus), plan.target);
                    continue;
                }
                changed = true;

                const auto av = ToActorValue(bonus);
                const float baseBefore = avo->GetBaseActorValue(av);
                const float permanentBefore = avo->GetPermanentActorValue(av);
                const float currentBefore = avo->GetActorValue(av);
                avo->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, av, plan.delta);
                s_state.applied[i] = plan.target;
                logger::info("[ST] Reconcile ({}): {} {:+.2f} (applied {:.2f} -> {:.2f}); "
                             "base {:.2f} -> {:.2f}, permanent {:.2f} -> {:.2f}, current {:.2f} -> {:.2f}.",
                    reason, TraitRules::BonusName(bonus), plan.delta, plan.target - plan.delta, plan.target,
                    baseBefore, avo->GetBaseActorValue(av), permanentBefore, avo->GetPermanentActorValue(av),
                    currentBefore, avo->GetActorValue(av));
            }
            if (changed || !quietWhenUnchanged) {
                LogStateLocked(reason);
            }
        }

        void OnGameSave(SKSE::SerializationInterface* intfc)
        {
            if (!intfc) {
                logger::error("[ST] Cosave: save callback received a null interface.");
                return;
            }
            std::lock_guard lock(s_mutex);
            const auto encoded = TraitSave::Encode(s_state);
            if (!intfc->WriteRecord(TraitSave::kRecordType, TraitSave::kVersion, encoded.data(),
                    static_cast<std::uint32_t>(encoded.size()))) {
                logger::error("[ST] Cosave: failed to write the trait record.");
                return;
            }
            logger::info("[ST] Cosave v{}: saved {}; applied {}; skill points granted {}.", TraitSave::kVersion,
                DescribeAllocation(s_state.allocation), DescribeApplied(s_state.applied), s_state.skillPointsGranted);
        }

        void OnGameLoad(SKSE::SerializationInterface* intfc)
        {
            std::lock_guard lock(s_mutex);
            s_state = {};
            s_gameActive = false;
            if (!intfc) {
                logger::error("[ST] Cosave: load callback received a null interface; state reset.");
                return;
            }

            std::optional<TraitSave::State> accepted;
            std::uint32_t type = 0, version = 0, length = 0;
            while (intfc->GetNextRecordInfo(type, version, length)) {
                if (type != TraitSave::kRecordType) {
                    logger::warn("[ST] Cosave: unknown record {:#010x} skipped.", type);
                    continue;
                }
                if (length == 0 || length > TraitSave::kMaxRecordSize) {
                    logger::warn("[ST] Cosave: rejected v{} trait record with unsafe length {}.", version, length);
                    continue;
                }
                std::vector<std::byte> data(length);
                const auto read = intfc->ReadRecordData(data.data(), length);
                if (read != length) {
                    logger::warn("[ST] Cosave: truncated v{} trait record (expected {}, read {}).", version, length, read);
                    continue;
                }
                const auto decoded = TraitSave::Decode(version, data);
                if (!decoded.Succeeded()) {
                    logger::warn("[ST] Cosave: rejected v{} trait record ({}, length {}).",
                        version, TraitSave::StatusName(decoded.status), length);
                    continue;
                }
                if (!TraitSave::AdoptFirstValid(accepted, decoded)) {
                    logger::warn("[ST] Cosave: duplicate trait record ignored.");
                }
            }

            if (accepted) {
                s_state = *accepted;
                logger::info("[ST] Cosave: restored {}; applied {}; skill points granted {}.",
                    DescribeAllocation(s_state.allocation), DescribeApplied(s_state.applied), s_state.skillPointsGranted);
            } else {
                logger::info("[ST] Cosave: no trait record; starting with no allocations and nothing applied.");
            }
        }

        void OnGameRevert(SKSE::SerializationInterface*)
        {
            std::lock_guard lock(s_mutex);
            s_state = {};
            s_gameActive = false;
            // Game settings outlive the save: put the original prices back.
            if (s_barterOriginal) {
                WriteBarter(*s_barterOriginal);
            }
            logger::info("[ST] Cosave: reverted; trait state cleared.");
        }
    }

    bool RegisterSerialization()
    {
        auto* serialization = SKSE::GetSerializationInterface();
        if (!serialization) {
            logger::critical("[ST] SKSE SerializationInterface unavailable; trait state cannot be saved.");
            return false;
        }
        serialization->SetUniqueID(TraitSave::kUniqueID);
        serialization->SetSaveCallback(OnGameSave);
        serialization->SetLoadCallback(OnGameLoad);
        serialization->SetRevertCallback(OnGameRevert);
        logger::info("[ST] Cosave v{} serialization registered.", TraitSave::kVersion);
        return true;
    }

    void OnNewGame()
    {
        std::lock_guard lock(s_mutex);
        s_state = {};
        s_gameActive = true;
        logger::info("[ST] New game: no trait allocations.");
        LogStateLocked("new-game");
    }

    void Reconcile(std::string_view reason)
    {
        std::lock_guard lock(s_mutex);
        s_gameActive = true;
        ReconcileLocked(reason);
    }

    void ReconcileIfActive(std::string_view reason)
    {
        std::lock_guard lock(s_mutex);
        if (s_gameActive) {
            ReconcileLocked(reason, true);
        }
    }

    void CaptureGameSettings()
    {
        std::lock_guard lock(s_mutex);
        s_barterOriginal = ReadBarter();
        s_barterWritten = s_barterOriginal;
        if (s_barterOriginal) {
            logger::info("[ST] Charisma: original fBarterMin={:.3f}, fBarterMax={:.3f}.",
                s_barterOriginal->barterMin, s_barterOriginal->barterMax);
        } else {
            logger::error("[ST] Charisma: fBarterMin/fBarterMax not found; Charisma has no effect.");
        }
        logger::info("[ST] Agility: {:.2f} CriticalChance per 1% crit chance (fWeaponConditionCriticalChanceMult).",
            CriticalChancePerPercent());
    }

    void SetSettings(const TraitRules::TraitSettings& settings)
    {
        std::lock_guard lock(s_mutex);
        s_settings = settings;
    }

    std::int32_t TakeSkillPointBonus(std::uint32_t level)
    {
        std::lock_guard lock(s_mutex);
        const auto intelligence = s_state.allocation[static_cast<std::size_t>(TraitRules::Trait::kIntelligence)];
        const auto owed = TraitRules::SkillPointsOwed(
            intelligence, s_settings.intelligenceSkillPoints, level, s_state.skillPointsGranted);
        if (owed > 0) {
            s_state.skillPointsGranted += static_cast<std::uint32_t>(owed);
        }
        logger::info("[ST] Intelligence: level {}, {} points x {:.3f}/level: +{} skill points (granted {} in total).",
            level, intelligence, s_settings.intelligenceSkillPoints, owed, s_state.skillPointsGranted);
        return owed;
    }

    std::optional<TraitRules::AllocationError> CommitAllocation(
        const TraitRules::Allocation& proposed, std::string_view source)
    {
        std::lock_guard lock(s_mutex);
        if (!s_gameActive) {
            logger::info("[ST] Commit ({}): no game loaded.", source);
            return std::nullopt;
        }
        const auto earned = TraitRules::EarnedPoints(PlayerLevel(), s_settings.points);
        const auto result = TraitRules::ValidateAllocation(s_state.allocation, proposed, earned);
        if (result != TraitRules::AllocationError::kNone) {
            logger::warn("[ST] Commit ({}): rejected ({}); kept {}.", source,
                TraitRules::AllocationErrorName(result), DescribeAllocation(s_state.allocation));
            return result;
        }
        if (proposed == s_state.allocation) {
            logger::info("[ST] Commit ({}): no change.", source);
            LogStateLocked(source);
            return result;
        }
        logger::info("[ST] Commit ({}): {} -> {}.", source, DescribeAllocation(s_state.allocation),
            DescribeAllocation(proposed));
        s_state.allocation = proposed;
        ReconcileLocked(source);
        return result;
    }

    std::optional<TraitRules::AllocationError> SpendPoint(TraitRules::Trait trait)
    {
        const auto index = static_cast<std::size_t>(trait);
        if (index >= TraitRules::kTraitCount) {
            return TraitRules::AllocationError::kExceedsUnspent;
        }
        TraitRules::Allocation proposed;
        {
            std::lock_guard lock(s_mutex);
            proposed = s_state.allocation;
        }
        ++proposed[index];
        return CommitAllocation(proposed, std::format("spend {}", TraitRules::TraitKey(trait)));
    }

    Snapshot GetSnapshot()
    {
        std::lock_guard lock(s_mutex);
        Snapshot snapshot;
        snapshot.gameActive = s_gameActive;
        snapshot.allocation = s_state.allocation;
        snapshot.applied = s_state.applied;
        snapshot.skillPointsGranted = s_state.skillPointsGranted;
        const auto charisma = s_state.allocation[static_cast<std::size_t>(TraitRules::Trait::kCharisma)];
        snapshot.priceFactorScale = static_cast<float>(
            std::max(0.0, 1.0 - static_cast<double>(charisma) * std::max(0.0f, s_settings.charismaPriceImprovement)));
        if (s_gameActive) {
            snapshot.level = PlayerLevel();
            snapshot.earned = TraitRules::EarnedPoints(snapshot.level, s_settings.points);
            snapshot.spent = TraitRules::SpentPoints(snapshot.allocation);
            snapshot.unspent = TraitRules::UnspentPoints(snapshot.level, s_settings.points, snapshot.allocation);
        }
        return snapshot;
    }
}
