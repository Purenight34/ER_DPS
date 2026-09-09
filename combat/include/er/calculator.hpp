#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace er {

struct Contribution {
    std::string source_id;
    double value{};
};

struct StatInput {
    double base{};
    double per_level{};
    std::vector<Contribution> flat;
};

struct Combatant {
    std::string character_id;
    std::string weapon_id;
    std::string equipment;
    std::string traits;
    std::string masteries;
    std::string skill_levels;
    std::string initial_effects;
    int level{};
    StatInput attack_power;
    StatInput defense;
    StatInput max_hp;
};

struct Attack {
    std::string id;
    std::int64_t time_ms{};
    bool hit{};
    bool critical{};
    std::optional<double> observed_damage;
};

struct Scenario {
    std::string id;
    std::string patch_version;
    std::string data_version;
    std::string data_source;
    std::string formula_version;
    std::string game_mode;
    std::string notes;
    Combatant attacker;
    Combatant defender;
    double attack_speed{}; // Explicit final attacks/second, not a growth formula.
    double critical_chance{}; // Fraction [0, 1], not a percentage.
    double critical_multiplier{}; // Explicit final multiplier, not bonus damage.
    std::string amplification_level_source; // Explicit level basis, never inferred.
    int amplification_levels{}; // Number of level increments to apply.
    double amplification_per_level{}; // Fraction: 0.02 means 2% per level.
    double initial_hp{};
    double initial_shield{};
    std::int64_t duration_ms{}; // Measurement interval is [0, duration_ms).
    bool unsupported_effects{};
    std::vector<Attack> attacks;
};

struct StatResult {
    double base{};
    double growth{};
    double flat{};
    double total{};
};

struct PermanentStats {
    StatResult attack_power;
    StatResult defense;
    StatResult max_hp;
};

struct Comparison {
    double observed{};
    double delta{}; // Calculated minus observed.
    std::optional<double> percent;
};

struct ImpactResult {
    std::string id;
    std::int64_t time_ms{};
    std::size_t sequence_index{};
    bool hit{};
    bool critical{};
    double raw_damage{};
    double defense_multiplier{};
    double total_damage{};
    double shield_damage{};
    double health_damage{};
    double remaining_shield{};
    double remaining_hp{};
    double current_hp{};
    std::optional<Comparison> comparison;
};

struct Result {
    PermanentStats attacker;
    PermanentStats defender;
    double basic_attack_amplification{};
    std::vector<ImpactResult> impacts;
    double total_damage{};
    double shield_damage{};
    double health_damage{};
    double remaining_hp{};
    double remaining_shield{};
    double current_hp{};
    double combo_dps{};
    std::size_t measured_count{};
    double measured_calculated_damage{};
    std::optional<Comparison> measured_comparison;
    std::vector<std::string> warnings;
};

// A fresh, isolated runtime is used for each invocation. Throws invalid_argument
// for unsupported rules or invalid/incomplete input, and overflow_error for
// non-finite derived values. No HTTP, files, UI, RNG, or LLM dependencies.
Result simulate(const Scenario& scenario);

} // namespace er
