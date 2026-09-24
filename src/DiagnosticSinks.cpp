#include "PCH.h"
#include "DiagnosticSinks.h"

#include <chrono>

namespace ST::DiagnosticSinks {
    namespace {
        constexpr std::uint64_t kSummaryInterval = 10;

        // One weapon swing can raise several TESHitEvents in the same frame
        // (an enchanted weapon's enchantment hit also names the weapon as its
        // source, and carries no attack flags). Events for the same target
        // and weapon this close together are merged into one swing.
        constexpr auto kSameSwingWindow = std::chrono::milliseconds(100);

        // TESHitEvent::Flag occupies the low four bits; the rest is noise.
        constexpr std::uint8_t kKnownHitFlags = 0x0F;
        constexpr std::uint8_t kPowerAttackFlag = static_cast<std::uint8_t>(RE::TESHitEvent::Flag::kPowerAttack);

        struct Counter {
            std::uint64_t swings{ 0 };
            std::uint64_t crits{ 0 };

            [[nodiscard]] std::string Format() const
            {
                const double rate = swings == 0 ? 0.0 : 100.0 * static_cast<double>(crits) / static_cast<double>(swings);
                return std::format("{} crits / {} swings ({:.1f}%)", crits, swings, rate);
            }
        };

        // Event sinks run on the main thread; the state needs no locking.
        Counter s_normal;
        Counter s_power;

        struct LastSwing {
            RE::FormID                            target{ 0 };
            RE::FormID                            weapon{ 0 };
            std::chrono::steady_clock::time_point time{};
            std::uint8_t                          flags{ 0 };
        };
        LastSwing s_lastSwing;
        bool      s_registered = false;

        std::string Describe(const RE::TESForm* form)
        {
            if (!form) {
                return "none";
            }
            const char* name = form->GetName();
            return std::format("'{}' ({:08X})", name && name[0] ? name : "unnamed", form->GetFormID());
        }

        std::string CurrentCriticalChance()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* avo = player ? player->AsActorValueOwner() : nullptr;
            return avo ? std::format("{:.2f}", avo->GetActorValue(RE::ActorValue::kCriticalChance)) : "unavailable";
        }

        std::string Totals()
        {
            return std::format("normal attacks {}; power attacks {}", s_normal.Format(), s_power.Format());
        }

        bool IsPowerAttack(std::uint8_t flags)
        {
            return (flags & kPowerAttackFlag) != 0;
        }

        // Counts the player's weapon swings, split into normal and power
        // attacks, so each critical-hit line can show the observed rates.
        // Spell and enchantment hits with a non-weapon source are skipped.
        struct HitSink final : RE::BSTEventSink<RE::TESHitEvent> {
            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) override
            {
                if (!event || !event->cause || !event->cause->IsPlayerRef()) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                const auto* source = RE::TESForm::LookupByID(event->source);
                if (!source || !source->Is(RE::FormType::Weapon)) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                const auto now = std::chrono::steady_clock::now();
                const auto targetID = event->target ? event->target->GetFormID() : 0;
                const auto flags = static_cast<std::uint8_t>(event->flags.underlying() & kKnownHitFlags);
                const bool sameSwing = s_lastSwing.target == targetID && s_lastSwing.weapon == event->source &&
                                       now - s_lastSwing.time <= kSameSwingWindow;
                if (sameSwing) {
                    // A later event of the swing may reveal the power attack;
                    // move the swing to the power counter once.
                    if (IsPowerAttack(flags) && !IsPowerAttack(s_lastSwing.flags)) {
                        --s_normal.swings;
                        ++s_power.swings;
                    }
                    s_lastSwing.flags |= flags;
                } else {
                    // Every earlier swing is complete now, so the periodic
                    // totals never catch a swing before its power flag.
                    const auto completed = s_normal.swings + s_power.swings;
                    if (completed > 0 && completed % kSummaryInterval == 0) {
                        logger::info("[ST] Diagnostics: {}; CriticalChance={}.", Totals(), CurrentCriticalChance());
                    } else if (completed == 0) {
                        logger::info("[ST] Diagnostics: first player weapon swing; CriticalChance={}.",
                            CurrentCriticalChance());
                    }
                    s_lastSwing = { targetID, event->source, now, flags };
                    ++(IsPowerAttack(flags) ? s_power : s_normal).swings;
                }

                const auto* targetActor = event->target ? event->target->As<RE::Actor>() : nullptr;
                logger::trace("[ST] Hit: player -> {} ({}) with {}, flags={:#x}{}.",
                    Describe(event->target.get()),
                    targetActor ? (targetActor->IsDead() ? "dead actor" : "living actor") : "not an actor",
                    Describe(source), flags, sameSwing ? " (same swing)" : "");
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        struct CriticalHitSink final : RE::BSTEventSink<RE::CriticalHit::Event> {
            RE::BSEventNotifyControl ProcessEvent(
                const RE::CriticalHit::Event* event, RE::BSTEventSource<RE::CriticalHit::Event>*) override
            {
                if (!event) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                const bool byPlayer = event->aggressor && event->aggressor->IsPlayerRef();
                const auto* weapon = event->weapon;
                const auto weaponCritical = weapon
                    ? std::format("crit mult={:.2f}, crit damage={}",
                          weapon->criticalData.prcntMult, weapon->criticalData.damage)
                    : std::string("no weapon data");

                if (!byPlayer) {
                    logger::debug("[ST] Critical hit: {} with {}, sneak={}; {}.",
                        Describe(event->aggressor), Describe(weapon), event->sneakHit, weaponCritical);
                    return RE::BSEventNotifyControl::kContinue;
                }

                // The crit follows the hit events of its swing in the same frame.
                const bool power = IsPowerAttack(s_lastSwing.flags);
                ++(power ? s_power : s_normal).crits;
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* avo = player ? player->AsActorValueOwner() : nullptr;
                const auto critChance = avo
                    ? std::format("CriticalChance current={:.2f}, permanent={:.2f}, base={:.2f}",
                          avo->GetActorValue(RE::ActorValue::kCriticalChance),
                          avo->GetPermanentActorValue(RE::ActorValue::kCriticalChance),
                          avo->GetBaseActorValue(RE::ActorValue::kCriticalChance))
                    : std::string("CriticalChance unavailable");
                logger::info("[ST] Critical hit: player {} with {}, sneak={}; {}; {}; {}.",
                    power ? "power attack" : "normal attack", Describe(weapon), event->sneakHit,
                    critChance, weaponCritical, Totals());
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        HitSink         s_hitSink;
        CriticalHitSink s_criticalHitSink;

        // Logs every game setting whose name mentions critical hits, to learn
        // what besides the CriticalChance actor value feeds the crit roll.
        void LogCriticalGameSettings()
        {
            auto* collection = RE::GameSettingCollection::GetSingleton();
            if (!collection) {
                logger::warn("[ST] Diagnostics: GameSettingCollection unavailable; critical settings not listed.");
                return;
            }
            std::vector<std::string> lines;
            for (const auto& item : collection->settings) {
                const char* name = item.first;
                const auto* setting = item.second;
                if (!setting || !name) {
                    continue;
                }
                std::string lower(name);
                std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower.find("crit") == std::string::npos) {
                    continue;
                }
                std::string value;
                switch (setting->GetType()) {
                    case RE::Setting::Type::kFloat: value = std::format("{}", setting->GetFloat()); break;
                    case RE::Setting::Type::kInteger: value = std::format("{}", setting->GetInteger()); break;
                    case RE::Setting::Type::kUnsignedInteger: value = std::format("{}", setting->GetUnsignedInteger()); break;
                    case RE::Setting::Type::kBool: value = setting->GetBool() ? "true" : "false"; break;
                    default: value = "(other type)"; break;
                }
                lines.push_back(std::format("{}={}", name, value));
            }
            std::ranges::sort(lines);
            logger::info("[ST] Diagnostics: {} game settings mention 'crit'.", lines.size());
            for (const auto& line : lines) {
                logger::info("[ST] Diagnostics:   {}", line);
            }
        }
    }

    void Register()
    {
        if (s_registered) {
            return;
        }
        s_registered = true;

        if (auto* scripts = RE::ScriptEventSourceHolder::GetSingleton()) {
            scripts->AddEventSink<RE::TESHitEvent>(&s_hitSink);
            logger::info("[ST] Diagnostics: hit counter registered.");
        } else {
            logger::warn("[ST] Diagnostics: ScriptEventSourceHolder unavailable; hit counter not registered.");
        }

        if (auto* criticalHits = RE::CriticalHit::GetEventSource()) {
            criticalHits->AddEventSink(&s_criticalHitSink);
            logger::info("[ST] Diagnostics: critical-hit logger registered.");
        } else {
            logger::warn("[ST] Diagnostics: CriticalHit event source unavailable; critical hits not logged.");
        }

        LogCriticalGameSettings();
    }

    void Reset()
    {
        if (s_normal.swings || s_power.swings) {
            logger::info("[ST] Diagnostics: session totals - {}.", Totals());
        }
        s_normal = {};
        s_power = {};
        s_lastSwing = {};
    }
}
