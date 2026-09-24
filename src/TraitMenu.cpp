#include "PCH.h"
#include "TraitMenu.h"

#include "Config.h"
#include "SALBridge.h"
#include "TraitMenuRules.h"
#include "TraitState.h"

#include <SKSE/Translation.h>

#include <array>
#include <atomic>
#include <string>

namespace ST::TraitMenu {
    namespace {
        using TraitMenuRules::SessionState;
        using TraitRules::Trait;

        constexpr std::string_view kMenuName = "ST Trait Menu";
        constexpr std::string_view kSwfName = "ST_TraitMenu";

        TraitMenuRules::Session           s_session;
        TraitMenuRules::ContinuationGuard s_continuation;
        std::atomic<std::uint64_t>        s_generation{ 1 };
        RE::GFxMovieView*                 s_activeMovie{ nullptr };
        bool                              s_registered{ false };
        bool                              s_movieLoaded{ false };

        struct Text {
            std::string title{ "Distribute Trait Points" };
            std::string confirm{ "Confirm" };
            std::string reset{ "Reset" };
            std::string level{ "Level" };
            std::string remaining{ "points remaining" };
            std::string carryNote{ "Unspent points are kept for your next level." };
            std::string permanentNote{ "Trait points are permanent once confirmed." };
            std::string inactive{ "No effect yet" };
            std::string hint{ "Up/Down: select    Right, Enter or +: add    Left, Backspace or -: remove    R: reset    C or Esc: confirm" };
            std::array<std::string, TraitRules::kTraitCount> names{
                "Strength", "Resilience", "Agility", "Intelligence", "Wisdom", "Charisma"
            };
            std::array<std::string, TraitRules::kTraitCount> effects{
                "+{value}% of base Stamina per point", "+{value}% of base Health per point",
                "+{value}% critical hit chance per point",
                "+{value} skill points per level per point, retroactive", "+{value}% of base Magicka per point",
                "{value}% better buy and sell prices per point"
            };
        };
        Text s_text;

        constexpr std::array<std::string_view, TraitRules::kTraitCount> kTraitTokens{
            "STRENGTH", "RESILIENCE", "AGILITY", "INTELLIGENCE", "WISDOM", "CHARISMA"
        };

        void LoadTranslations()
        {
            SKSE::Translation::ParseTranslation("SimpleTraits");
            const auto translate = [](std::string_view key, std::string& target) {
                std::string result;
                if (SKSE::Translation::Translate(std::string(key), result) && !result.empty()) {
                    target = result;
                } else {
                    logger::warn("[ST] TraitMenu: translation {} unavailable; using English fallback.", key);
                }
            };
            translate("$ST_MENU_TITLE", s_text.title);
            translate("$ST_CONFIRM", s_text.confirm);
            translate("$ST_RESET", s_text.reset);
            translate("$ST_LEVEL", s_text.level);
            translate("$ST_REMAINING", s_text.remaining);
            translate("$ST_CARRY_NOTE", s_text.carryNote);
            translate("$ST_PERMANENT_NOTE", s_text.permanentNote);
            translate("$ST_EFFECT_INACTIVE", s_text.inactive);
            translate("$ST_HINT", s_text.hint);
            for (std::size_t i = 0; i < TraitRules::kTraitCount; ++i) {
                translate(std::format("$ST_TRAIT_{}", kTraitTokens[i]), s_text.names[i]);
            }
            translate("$ST_EFFECT_STRENGTH", s_text.effects[0]);
            translate("$ST_EFFECT_RESILIENCE", s_text.effects[1]);
            translate("$ST_EFFECT_AGILITY", s_text.effects[2]);
            translate("$ST_EFFECT_INTELLIGENCE", s_text.effects[3]);
            translate("$ST_EFFECT_WISDOM", s_text.effects[4]);
            translate("$ST_EFFECT_CHARISMA", s_text.effects[5]);
        }

        // "5", "2.5": per-point values without trailing zeros.
        std::string FormatValue(float value)
        {
            auto text = std::format("{:.2f}", value);
            while (!text.empty() && text.back() == '0') text.pop_back();
            if (!text.empty() && text.back() == '.') text.pop_back();
            return text;
        }

        std::string EffectText(std::size_t trait)
        {
            float perPoint = 0.0f;
            switch (static_cast<Trait>(trait)) {
                case Trait::kStrength: perPoint = Config::traits.staminaPercent * 100.0f; break;
                case Trait::kResilience: perPoint = Config::traits.healthPercent * 100.0f; break;
                case Trait::kWisdom: perPoint = Config::traits.magickaPercent * 100.0f; break;
                case Trait::kAgility: perPoint = Config::traits.criticalChancePerPoint; break;
                case Trait::kCharisma: perPoint = Config::traits.charismaPriceImprovement * 100.0f; break;
                case Trait::kIntelligence:
                    if (!SALBridge::HasSkillPointBonus()) {
                        return s_text.inactive;  // needs SAL API V3
                    }
                    perPoint = Config::traits.intelligenceSkillPoints;
                    break;
            }
            auto text = s_text.effects[trait];
            if (const auto pos = text.find("{value}"); pos != std::string::npos) {
                text.replace(pos, 7, FormatValue(perPoint));
            }
            return text;
        }

        [[nodiscard]] RE::GFxMovieView* ActiveMovie()
        {
            if (s_activeMovie) {
                return s_activeMovie;
            }
            auto* ui = RE::UI::GetSingleton();
            const auto menu = ui ? ui->GetMenu(kMenuName) : nullptr;
            return menu && menu->uiMovie ? menu->uiMovie.get() : nullptr;
        }

        // Every exit path ends here; SAL is resumed at most once per wait.
        void FinishStep(std::string_view reason)
        {
            if (s_continuation.TakeContinue()) {
                logger::info("[ST] TraitMenu: step finished (reason={}); continuing SAL's level-up.", reason);
                SALBridge::ContinueLevelUp();
            } else {
                logger::debug("[ST] TraitMenu: step already finished (reason={}).", reason);
            }
        }

        void CloseAndFinish(std::string_view reason)
        {
            s_session.MarkClosing();
            if (auto* movie = ActiveMovie()) {
                movie->Invoke("ST_SetClosing", nullptr, nullptr, 0);
            }
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            } else {
                logger::warn("[ST] TraitMenu: UIMessageQueue unavailable while closing the menu.");
            }
            s_session.Cancel();
            FinishStep(reason);
        }

        [[nodiscard]] bool InvokeInit(RE::GFxMovieView* movie)
        {
            if (!movie || s_session.State() != SessionState::kActive) {
                return false;
            }
            RE::GFxValue traits;
            movie->CreateArray(&traits);
            for (std::size_t i = 0; i < TraitRules::kTraitCount; ++i) {
                RE::GFxValue object;
                movie->CreateObject(&object);
                object.SetMember("id", RE::GFxValue(static_cast<int>(i)));
                object.SetMember("name", RE::GFxValue(s_text.names[i].c_str()));
                object.SetMember("effect", RE::GFxValue(EffectText(i).c_str()));
                object.SetMember("active", RE::GFxValue(EffectText(i) != s_text.inactive));
                object.SetMember("points", RE::GFxValue(static_cast<double>(s_session.Committed(i))));
                traits.PushBack(object);
            }

            RE::GFxValue info;
            movie->CreateObject(&info);
            auto* player = RE::PlayerCharacter::GetSingleton();
            info.SetMember("level", RE::GFxValue(player ? static_cast<int>(player->GetLevel()) : 0));
            info.SetMember("title", RE::GFxValue(s_text.title.c_str()));
            info.SetMember("confirmLabel", RE::GFxValue(s_text.confirm.c_str()));
            info.SetMember("resetLabel", RE::GFxValue(s_text.reset.c_str()));
            info.SetMember("levelLabel", RE::GFxValue(s_text.level.c_str()));
            info.SetMember("remainingLabel", RE::GFxValue(s_text.remaining.c_str()));
            info.SetMember("carryNote", RE::GFxValue(s_text.carryNote.c_str()));
            info.SetMember("permanentNote", RE::GFxValue(s_text.permanentNote.c_str()));
            info.SetMember("hint", RE::GFxValue(s_text.hint.c_str()));

            std::array<RE::GFxValue, 3> args{ traits, RE::GFxValue(static_cast<double>(s_session.TotalPoints())), info };
            if (!movie->Invoke("ST_Init", nullptr, args.data(), static_cast<std::uint32_t>(args.size()))) {
                logger::error("[ST] TraitMenu: required ST_Init function is missing from the SWF.");
                return false;
            }
            logger::info("[ST] TraitMenu: session opened with {} unspent points.", s_session.TotalPoints());
            return true;
        }

        void InvokeUpdate(std::size_t trait)
        {
            auto* movie = ActiveMovie();
            if (!movie) {
                return;
            }
            std::array<RE::GFxValue, 4> args{
                RE::GFxValue(static_cast<int>(trait)),
                RE::GFxValue(static_cast<double>(s_session.Preview(trait))),
                RE::GFxValue(static_cast<double>(s_session.Delta(trait))),
                RE::GFxValue(static_cast<double>(s_session.RemainingPoints()))
            };
            if (!movie->Invoke("ST_UpdateTrait", nullptr, args.data(), static_cast<std::uint32_t>(args.size()))) {
                logger::warn("[ST] TraitMenu: ST_UpdateTrait is missing from the active SWF.");
            }
        }

        void Confirm()
        {
            if (s_session.State() != SessionState::kActive) {
                logger::debug("[ST] TraitMenu: duplicate or stale confirm ignored.");
                return;
            }
            const auto snapshot = TraitState::GetSnapshot();
            const auto plan = s_session.PrepareCommit(snapshot.allocation);
            if (!plan.Ready()) {
                logger::warn("[ST] TraitMenu: commit abandoned ({}); trait points unchanged.",
                    plan.status == TraitMenuRules::CommitStatus::kSnapshotDrift ? "allocation changed while open"
                                                                               : "invalid state");
                CloseAndFinish("commit-abandoned");
                return;
            }
            const auto result = TraitState::CommitAllocation(plan.allocation, "trait menu");
            if (!result || *result != TraitRules::AllocationError::kNone) {
                logger::warn("[ST] TraitMenu: commit rejected; trait points unchanged.");
            } else {
                logger::info("[ST] TraitMenu: confirmed; {} points kept for later.", plan.remaining);
            }
            CloseAndFinish("confirmed");
        }

        void Preview(std::size_t trait, bool add)
        {
            const auto result = add ? s_session.Allocate(trait) : s_session.Deallocate(trait);
            if (result != TraitMenuRules::PreviewResult::kChanged) {
                logger::debug("[ST] TraitMenu: {} {} rejected ({}).", add ? "add" : "remove",
                    TraitRules::TraitKey(static_cast<Trait>(trait)), static_cast<int>(result));
                return;
            }
            InvokeUpdate(trait);
            logger::info("[ST] TraitMenu: preview {} {} -> {} (+{} this level); {} points remain.",
                add ? "add" : "remove", TraitRules::TraitKey(static_cast<Trait>(trait)), s_session.Preview(trait),
                s_session.Delta(trait), s_session.RemainingPoints());
        }

        void ResetPreview()
        {
            if (!s_session.Reset()) {
                return;
            }
            for (std::size_t i = 0; i < TraitRules::kTraitCount; ++i) {
                InvokeUpdate(i);
            }
            logger::info("[ST] TraitMenu: preview reset; {} points restored.", s_session.RemainingPoints());
        }

        [[nodiscard]] bool IsValidCallback(const RE::FxDelegateArgs& args, std::uint32_t expectedArgs)
        {
            if (s_session.State() != SessionState::kActive || !s_activeMovie || args.GetMovie() != s_activeMovie ||
                args.GetArgCount() != expectedArgs) {
                logger::warn("[ST] TraitMenu: rejected stale or malformed Scaleform callback.");
                return false;
            }
            return true;
        }

        [[nodiscard]] std::optional<std::size_t> TraitArgument(const RE::FxDelegateArgs& args)
        {
            if (!IsValidCallback(args, 1) || !args[0].IsNumber()) {
                return std::nullopt;
            }
            const auto trait = TraitMenuRules::ParseTraitIndex(args[0].GetNumber());
            if (!trait) {
                logger::warn("[ST] TraitMenu: rejected invalid trait identifier.");
            }
            return trait;
        }

        class Menu final : public RE::IMenu {
        public:
            static RE::stl::owner<RE::IMenu*> Creator() { return new Menu(); }

            Menu()
            {
                auto* scaleform = RE::BSScaleformManager::GetSingleton();
                s_movieLoaded = scaleform && scaleform->LoadMovie(this, uiMovie, kSwfName.data());
                if (!s_movieLoaded) {
                    logger::error("[ST] TraitMenu: failed to load Interface/ST_TraitMenu.swf.");
                }
                menuFlags |= RE::UI_MENU_FLAGS::kPausesGame;
                menuFlags |= RE::UI_MENU_FLAGS::kModal;
                menuFlags |= RE::UI_MENU_FLAGS::kDisablePauseMenu;
                menuFlags |= RE::UI_MENU_FLAGS::kUsesCursor;
                depthPriority = 3;
                inputContext = Context::kMenuMode;
            }

            void PostCreate() override
            {
                s_activeMovie = uiMovie.get();
                if (!s_movieLoaded || !s_activeMovie || !InvokeInit(s_activeMovie)) {
                    CloseAndFinish("swf-initialization-failed");
                }
            }

            void Accept(CallbackProcessor* callbacks) override
            {
                if (!callbacks) {
                    logger::error("[ST] TraitMenu: Scaleform callback processor is null.");
                    return;
                }
                callbacks->Process("ST_OnAllocate", OnAllocate);
                callbacks->Process("ST_OnDeallocate", OnDeallocate);
                callbacks->Process("ST_OnConfirm", OnConfirm);
                callbacks->Process("ST_OnReset", OnReset);
            }

        private:
            static void OnAllocate(const RE::FxDelegateArgs& args)
            {
                if (const auto trait = TraitArgument(args)) {
                    Preview(*trait, true);
                }
            }

            static void OnDeallocate(const RE::FxDelegateArgs& args)
            {
                if (const auto trait = TraitArgument(args)) {
                    Preview(*trait, false);
                }
            }

            static void OnConfirm(const RE::FxDelegateArgs& args)
            {
                if (IsValidCallback(args, 0)) {
                    Confirm();
                }
            }

            static void OnReset(const RE::FxDelegateArgs& args)
            {
                if (IsValidCallback(args, 0)) {
                    ResetPreview();
                }
            }
        };

        // Watches ST's menu and the vanilla LevelUp Menu. An unexpected close
        // of ST's menu (another mod, a crash-guard) commits what the player
        // previewed, like SAL's skill menu, and always resumes SAL.
        struct MenuWatcher final : RE::BSTEventSink<RE::MenuOpenCloseEvent> {
            RE::BSEventNotifyControl ProcessEvent(
                const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (!event || event->opening) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                // The vanilla attribute choice raises a base value; the
                // percentage bonuses follow it. SAL's interim closes of the
                // same menu reconcile to no change.
                if (event->menuName == RE::LevelUpMenu::MENU_NAME) {
                    if (auto* tasks = SKSE::GetTaskInterface()) {
                        tasks->AddTask([]() { TraitState::ReconcileIfActive("level-up-menu-closed"); });
                    }
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (event->menuName != kMenuName) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                s_activeMovie = nullptr;
                if (s_session.State() == SessionState::kActive) {
                    const auto generation = s_generation.load();
                    if (auto* tasks = SKSE::GetTaskInterface()) {
                        tasks->AddTask([generation]() {
                            if (s_generation.load() == generation && s_session.State() == SessionState::kActive) {
                                logger::warn("[ST] TraitMenu: menu closed without confirm; committing the preview.");
                                Confirm();
                            }
                        });
                    } else {
                        Confirm();
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
        MenuWatcher s_watcher;

        void Open(std::uint64_t generation)
        {
            if (s_generation.load() != generation || s_session.State() != SessionState::kOpening) {
                logger::debug("[ST] TraitMenu: stale open discarded.");
                return;
            }
            const auto snapshot = TraitState::GetSnapshot();
            if (!snapshot.gameActive || snapshot.unspent <= 0 || !s_session.Begin(snapshot.unspent, snapshot.allocation)) {
                logger::warn("[ST] TraitMenu: nothing to allocate when opening; continuing.");
                s_session.Cancel();
                FinishStep("nothing-to-allocate");
                return;
            }
            auto* queue = RE::UIMessageQueue::GetSingleton();
            if (!queue) {
                logger::error("[ST] TraitMenu: UIMessageQueue unavailable; menu not shown.");
                s_session.Cancel();
                FinishStep("queue-unavailable");
                return;
            }
            queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kShow, nullptr);
        }
    }

    bool Register()
    {
        if (s_registered) {
            return true;
        }
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            logger::error("[ST] TraitMenu: UI singleton unavailable; the trait menu is disabled.");
            return false;
        }
        LoadTranslations();
        ui->Register(kMenuName, Menu::Creator);
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&s_watcher);
        s_registered = true;
        logger::info("[ST] TraitMenu: menu registered.");
        return SALBridge::RegisterLevelUpStep(WantsStep);
    }

    bool WantsStep(std::uint32_t level)
    {
        if (!s_registered) {
            return false;
        }
        const auto snapshot = TraitState::GetSnapshot();
        if (!snapshot.gameActive || snapshot.unspent <= 0) {
            logger::info("[ST] TraitMenu: level {}: no unspent trait points; step skipped.", level);
            return false;
        }
        if (!s_session.BeginOpening()) {
            logger::warn("[ST] TraitMenu: level {}: a session is already in progress; step skipped.", level);
            return false;
        }
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            logger::error("[ST] TraitMenu: task interface unavailable; step skipped.");
            s_session.Cancel();
            return false;
        }
        s_continuation.Arm();
        const auto generation = s_generation.load();
        tasks->AddTask([generation]() { Open(generation); });
        logger::info("[ST] TraitMenu: level {}: {} unspent trait points; opening the trait menu.", level,
            snapshot.unspent);
        return true;
    }

    void ResetState()
    {
        s_generation.fetch_add(1);
        s_session.Cancel();
        s_continuation.Reset();
        s_activeMovie = nullptr;
        auto* ui = RE::UI::GetSingleton();
        auto* queue = RE::UIMessageQueue::GetSingleton();
        if (ui && queue && ui->IsMenuOpen(kMenuName)) {
            queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
    }
}
