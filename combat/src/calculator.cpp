#include "er/calculator.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string_view>

namespace er {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::invalid_argument(message);
}
void text_required(const std::string& value, const std::string& name) {
    require(value.find_first_not_of(" \t\r\n") != std::string::npos, name + " is required");
}
void finite_input(double value, const std::string& name) {
    require(std::isfinite(value), name + " must be finite");
}
void nonnegative(double value, const std::string& name) {
    finite_input(value, name);
    require(value >= 0, name + " must be nonnegative");
}
double finite_result(double value, std::string_view name) {
    if (!std::isfinite(value)) throw std::overflow_error(std::string(name) + " overflow");
    return value;
}

StatResult stat(const StatInput& input, int level, const std::string& name) {
    finite_input(input.base, name + ".base");
    finite_input(input.per_level, name + ".per_level");
    StatResult out;
    out.base = input.base;
    out.growth = finite_result(input.per_level * (level - 1), name);
    std::set<std::string> sources;
    for (const auto& term : input.flat) {
        text_required(term.source_id, name + ".flat.source_id");
        require(sources.insert(term.source_id).second, name + ": duplicate flat source_id");
        finite_input(term.value, name + ".flat.value");
        out.flat = finite_result(out.flat + term.value, name);
    }
    out.total = finite_result(finite_result(out.base + out.growth, name) + out.flat, name);
    require(out.total >= 0, name + " total must be nonnegative (negative stats unsupported)");
    return out;
}

PermanentStats permanent(const Combatant& input, const std::string& side) {
    text_required(input.character_id, side + ".character_id");
    text_required(input.weapon_id, side + ".weapon_id");
    text_required(input.equipment, side + ".equipment");
    text_required(input.traits, side + ".traits");
    text_required(input.masteries, side + ".masteries");
    text_required(input.skill_levels, side + ".skill_levels");
    text_required(input.initial_effects, side + ".initial_effects");
    require(input.level >= 1, side + ".level must be at least 1");
    PermanentStats out{stat(input.attack_power, input.level, side + ".attack_power"),
                       stat(input.defense, input.level, side + ".defense"),
                       stat(input.max_hp, input.level, side + ".max_hp")};
    require(out.max_hp.total > 0, side + ".max_hp must be positive");
    return out;
}

Comparison compare(double calculated, double observed) {
    Comparison out{observed, finite_result(calculated - observed, "comparison delta"), std::nullopt};
    if (observed != 0) {
        // An unrepresentable relative error is left undefined; absolute error
        // remains useful and must never serialize as NaN/Infinity.
        const double percent = (out.delta / observed) * 100;
        if (std::isfinite(percent)) out.percent = percent;
    }
    return out;
}
} // namespace

PermanentStats calculate_permanent_stats(const Combatant& combatant) {
    return permanent(combatant, "combatant");
}

Result simulate(const Scenario& s) {
    text_required(s.id, "id");
    text_required(s.patch_version, "patch_version");
    text_required(s.data_version, "data_version");
    text_required(s.data_source, "data_source");
    text_required(s.game_mode, "game_mode");
    require(s.formula_version == "basic-attack-experiment-v1",
            "formula_version must be basic-attack-experiment-v1 (user-approved provisional model)");
    require(!s.unsupported_effects,
            "Unsupported scenario: penetration, extra impacts, passive procs, mode modifiers, "
            "or other damage modifiers require additional rules");
    require(s.amplification_level_source == "weapon_mastery",
            "amplification_level_source must be weapon_mastery");
    require(s.amplification_levels >= 0, "amplification_levels must be nonnegative");
    nonnegative(s.amplification_per_level, "amplification_per_level");
    finite_input(s.attack_speed, "attack_speed");
    if (s.attack_speed_known) {
        require(s.attack_speed > 0, "attack_speed must be positive final attacks/second");
    } else {
        require(s.attacks.size() == 1,
                "Final attack_speed is required to validate multiple basic attacks");
    }
    finite_input(s.critical_chance, "critical_chance");
    require(!s.critical_chance_known || (s.critical_chance >= 0 && s.critical_chance <= 1),
            "critical_chance must be a fraction in [0, 1]");
    finite_input(s.critical_multiplier, "critical_multiplier");
    require(!s.critical_multiplier_known || s.critical_multiplier >= 1,
            "critical_multiplier must be the final multiplier >= 1");
    nonnegative(s.initial_hp, "initial_hp");
    nonnegative(s.initial_shield, "initial_shield");
    require(s.duration_ms > 0, "duration_ms must be positive");
    require(!s.attacks.empty(), "At least one explicit attack is required");

    Result out;
    out.attacker = permanent(s.attacker, "attacker");
    out.defender = permanent(s.defender, "defender");
    require(s.initial_hp <= out.defender.max_hp.total, "initial_hp exceeds defender max_hp");
    out.basic_attack_amplification = finite_result(
        s.amplification_per_level * s.amplification_levels, "basic attack amplification");
    const double normal_raw = finite_result(out.attacker.attack_power.total *
        finite_result(1 + out.basic_attack_amplification, "amplification multiplier"), "raw damage");
    const double defense_multiplier = 100 / finite_result(100 + out.defender.defense.total, "defense");

    std::set<std::string> ids;
    std::vector<std::size_t> order(s.attacks.size());
    std::iota(order.begin(), order.end(), 0);
    for (const auto& attack : s.attacks) {
        text_required(attack.id, "attack.id");
        require(ids.insert(attack.id).second, "Duplicate attack ID: " + attack.id);
        require(attack.time_ms >= 0 && attack.time_ms < s.duration_ms,
                attack.id + ": time_ms must be inside [0, duration_ms)");
        if (attack.observed_damage) nonnegative(*attack.observed_damage, attack.id + ".observed_damage");
        if (attack.hit) {
            require(!attack.critical || s.critical_multiplier_known,
                    attack.id + ": final critical multiplier is required for a critical hit");
            require(!(s.critical_chance_known && attack.critical && s.critical_chance == 0),
                    attack.id + ": critical hit is impossible with critical_chance=0");
            require(!(s.critical_chance_known && !attack.critical && s.critical_chance == 1),
                    attack.id + ": normal hit is impossible with critical_chance=1");
        }
    }
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        return s.attacks[a].time_ms < s.attacks[b].time_ms;
    });
    for (std::size_t i = 1; i < order.size(); ++i) {
        const auto interval = s.attacks[order[i]].time_ms - s.attacks[order[i - 1]].time_ms;
        require(static_cast<long double>(interval) * s.attack_speed >= 1000.0L,
                s.attacks[order[i]].id + ": attack interval exceeds final attack_speed; "
                "the timeline was not changed");
    }

    out.remaining_hp = s.initial_hp;
    out.remaining_shield = s.initial_shield;
    double measured_observed = 0;
    for (auto index : order) {
        const auto& attack = s.attacks[index];
        ImpactResult impact;
        impact.id = attack.id;
        impact.time_ms = attack.time_ms;
        impact.sequence_index = index;
        impact.hit = attack.hit;
        impact.critical = attack.critical;
        impact.defense_multiplier = defense_multiplier;
        if (attack.hit) {
            impact.raw_damage = finite_result(normal_raw * (attack.critical ? s.critical_multiplier : 1),
                                               "critical raw damage");
            impact.total_damage = finite_result(impact.raw_damage * defense_multiplier, "damage");
        }
        impact.shield_damage = std::min(out.remaining_shield, impact.total_damage);
        impact.health_damage = impact.total_damage - impact.shield_damage;
        out.remaining_shield -= impact.shield_damage;
        out.remaining_hp = finite_result(out.remaining_hp - impact.health_damage, "remaining hp");
        impact.remaining_shield = out.remaining_shield;
        impact.remaining_hp = out.remaining_hp;
        impact.current_hp = std::max(0.0, out.remaining_hp);
        out.total_damage = finite_result(out.total_damage + impact.total_damage, "total damage");
        out.shield_damage = finite_result(out.shield_damage + impact.shield_damage, "shield damage");
        out.health_damage = finite_result(out.health_damage + impact.health_damage, "health damage");
        if (attack.observed_damage) {
            impact.comparison = compare(impact.total_damage, *attack.observed_damage);
            ++out.measured_count;
            out.measured_calculated_damage = finite_result(
                out.measured_calculated_damage + impact.total_damage, "measured calculated damage");
            measured_observed = finite_result(measured_observed + *attack.observed_damage, "measured damage");
        }
        out.impacts.push_back(impact);
    }
    out.current_hp = std::max(0.0, out.remaining_hp);
    out.combo_dps = finite_result(out.total_damage / (static_cast<double>(s.duration_ms) / 1000), "combo DPS");
    if (out.measured_count) out.measured_comparison = compare(out.measured_calculated_damage, measured_observed);
    out.warnings = {
        "PROVISIONAL: user-approved experimental formulas, not an official verified game calculator.",
        "Manual inputs: equipment/traits/masteries metadata do not automatically apply effects.",
        "Single-impact basic attacks only; animation, projectile and passive timing are not validated.",
        "Critical outcomes are explicit inputs, not a random sequence or probability-weighted expected damage.",
        "No critical history adjustment; no game rounding applied; use raw values for experiments.",
        "Shield is static; death does not stop the combo under the user-defined comparison rules."
    };
    if (out.measured_count < out.impacts.size()) {
        out.warnings.push_back("Incomplete measurements: comparison totals cover measured impacts only.");
    }
    if (!s.attack_speed_known) {
        out.warnings.push_back("Final attack speed is not supplied; a single hit has no inter-attack interval to validate.");
    }
    if (!s.critical_chance_known || !s.critical_multiplier_known) {
        out.warnings.push_back("Missing critical stats are marked unknown; no critical probability or multiplier was invented.");
    }
    return out;
}
} // namespace er
