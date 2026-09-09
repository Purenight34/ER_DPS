#pragma once
#include "er/catalog.hpp"
#include "er/calculator.hpp"
#include <optional>

namespace er {
enum class DefenderKind { Dummy, Character };

struct NamedLoadout {
    std::string character = "유키";
    std::string weapon = "녹슨 검";
    int level = 1;
    int weapon_mastery = 1;
    std::vector<std::string> armor;
    std::vector<std::string> traits;
    int yuki_buttons = 0;
};
struct NamedExperiment {
    NamedLoadout attacker;
    NamedLoadout defender;
    DefenderKind defender_kind = DefenderKind::Dummy;
    double dummy_max_hp = 1000;
    double dummy_defense = 0;
    std::int64_t duration_ms = 1000;
    std::vector<Attack> attacks = {{"aa1", 0, true, false, std::nullopt}};
    std::optional<double> final_attack_speed;
    std::optional<double> final_critical_chance;
    std::optional<double> final_critical_multiplier;
    std::optional<double> defender_initial_hp;
    double defender_initial_shield = 0;
};
struct ResolvedExperiment {
    Scenario scenario;
    DefenderKind defender_kind = DefenderKind::Dummy;
    std::vector<std::string> notices;
    std::vector<std::string> blockers;
};
ResolvedExperiment resolve_experiment(const Catalog& catalog, const NamedExperiment& input);
} // namespace er
