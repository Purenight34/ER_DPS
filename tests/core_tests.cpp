#include "er/calculator.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void check(bool condition, const std::string& name) {
    ++checks;
    if (!condition) throw std::runtime_error(name);
}
void near(double actual, double expected, const std::string& name) {
    check(std::abs(actual - expected) < 1e-10, name);
}
void rejects(const std::function<void()>& action, const std::string& name) {
    try { action(); }
    catch (const std::invalid_argument&) { ++checks; return; }
    catch (const std::overflow_error&) { ++checks; return; }
    throw std::runtime_error("Expected rejection: " + name);
}
er::Scenario fixture() {
    er::Scenario s;
    s.id = "synthetic-basic";
    s.patch_version = "synthetic-no-game-patch";
    s.data_version = "synthetic-fixture-v1";
    s.data_source = "synthetic unit test, not game data";
    s.formula_version = "basic-attack-experiment-v1";
    s.game_mode = "synthetic";
    s.notes = "Offline arithmetic fixture only";
    for (auto* c : {&s.attacker, &s.defender}) {
        c->character_id = c == &s.attacker ? "fixture-attacker" : "fixture-defender";
        c->weapon_id = "fixture-single-impact";
        c->equipment = "none";
        c->traits = "none";
        c->masteries = "explicit in scenario";
        c->skill_levels = "none";
        c->initial_effects = "none";
        c->level = 1;
        c->max_hp.base = 60;
    }
    s.attacker.level = 6;
    s.attacker.attack_power = {80, 2, {{"fixture-weapon", 10}}};
    s.defender.defense.base = 100;
    s.attack_speed = 1;
    s.critical_chance = .5;
    s.critical_multiplier = 1.75;
    s.amplification_level_source = "weapon_mastery";
    s.amplification_levels = 10;
    s.amplification_per_level = .02;
    s.initial_hp = 60;
    s.initial_shield = 10;
    s.duration_ms = 4000;
    s.attacks = {{"aa1", 0, true, false, 60},
                 {"aa2", 1000, true, true, 104},
                 {"aa3", 2000, false, false, std::nullopt},
                 {"aa4", 3000, true, false, 0}};
    return s;
}
}

int main() {
    try {
        auto s = fixture();
        const auto result = er::simulate(s);
        near(result.attacker.attack_power.base, 80, "base stat trace");
        near(result.attacker.attack_power.growth, 10, "level 6 has five growth increments");
        near(result.attacker.attack_power.flat, 10, "flat source trace");
        near(result.attacker.attack_power.total, 100, "permanent attack power");
        near(result.basic_attack_amplification, .2, "weapon mastery amplification");
        near(result.impacts[0].raw_damage, 120, "amplified normal attack");
        near(result.impacts[1].raw_damage, 210, "amplified critical attack");
        near(result.impacts[0].defense_multiplier, .5, "nonnegative defense multiplier");
        near(result.impacts[0].shield_damage, 10, "shield absorbs first");
        near(result.impacts[0].health_damage, 50, "shield overflow to health");
        near(result.impacts[1].remaining_hp, -95, "overkill is retained");
        near(result.impacts[2].total_damage, 0, "miss deals zero");
        near(result.impacts[3].health_damage, 60, "damage continues after death");
        near(result.total_damage, 225, "total combo damage");
        near(result.shield_damage + result.health_damage, result.total_damage, "no double count");
        near(result.remaining_hp, -155, "final bookkeeping hp");
        near(result.current_hp, 0, "condition hp clamped");
        near(result.combo_dps, 56.25, "explicit measurement window");
        check(result.measured_count == 3, "partial measurement count");
        near(result.measured_comparison->observed, 164, "observed subset only");
        near(result.measured_comparison->delta, 61, "comparison sign is calculated minus observed");
        check(!result.impacts[3].comparison->percent, "zero observed has no percentage");
        check(!result.impacts[2].comparison, "missing observation stays missing");
        near(s.initial_hp, 60, "scenario not mutated");
        near(s.attacker.attack_power.base, 80, "source stats not mutated");
        const auto again = er::simulate(s);
        near(again.total_damage, result.total_damage, "same input reproducible");
        near(again.impacts[0].shield_damage, 10, "fresh runtime shield");

        s.attacks[1].observed_damage.reset();
        const auto partial = er::simulate(s);
        near(partial.total_damage, 225, "unmeasured hit still deals damage");
        near(partial.measured_calculated_damage, 120, "unmeasured hit excluded from comparison");
        near(partial.measured_comparison->observed, 60, "measured observations only");
        check(partial.measured_count == 2, "measured count excludes unmeasured hit");
        s = fixture();

        s.attacks[0].hit = false;
        s.attacks[0].observed_damage.reset();
        const auto miss = er::simulate(s);
        near(miss.total_damage, 165, "hit toggle recalculates entire timeline");
        near(miss.impacts[1].shield_damage, 10, "miss leaves shield for next hit");
        near(miss.measured_calculated_damage, 165, "subset recalculated with hit toggles");

        s = fixture();
        s.attacks = {s.attacks[3], s.attacks[1], s.attacks[0], s.attacks[2]};
        const auto sorted = er::simulate(s);
        check(sorted.impacts[0].id == "aa1" && sorted.impacts[0].sequence_index == 2,
              "stable chronological ordering preserves input sequence");
        near(sorted.total_damage, 225, "explicit timestamps control chronology");

        s = fixture();
        s.attacker.level = 1;
        s.amplification_levels = 0;
        s.defender.defense.base = 0;
        s.attacks = {{"fraction", 0, true, false, std::nullopt}};
        s.attacker.attack_power = {80.25, 2, {{"fractional-source", .125}}};
        const auto fraction = er::simulate(s);
        near(fraction.attacker.attack_power.growth, 0, "level one no growth");
        near(fraction.total_damage, 80.375, "fractional precision retained");
        check(!fraction.measured_comparison, "no measured total when no observations");
        s.initial_shield = 100;
        const auto shield = er::simulate(s);
        near(shield.health_damage, 0, "fully shielded hit");
        near(shield.remaining_hp, 60, "shield protects bookkeeping hp");

        auto bad = [&](auto mutate, const char* name) {
            auto input = fixture(); mutate(input);
            rejects([&] { (void)er::simulate(input); }, name);
        };
        bad([](auto& x) { x.formula_version = "official-current"; }, "unknown rule version");
        bad([](auto& x) { x.unsupported_effects = true; }, "unsupported effects");
        bad([](auto& x) { x.patch_version.clear(); }, "missing provenance");
        bad([](auto& x) { x.attacker.level = 0; }, "invalid level");
        bad([](auto& x) { x.defender.defense.base = -1; }, "negative defense");
        bad([](auto& x) { x.initial_hp = 61; }, "initial hp exceeds max");
        bad([](auto& x) { x.initial_shield = -1; }, "negative shield");
        bad([](auto& x) { x.duration_ms = 0; }, "zero window");
        bad([](auto& x) { x.attacks[0].time_ms = -1; }, "negative event time");
        bad([](auto& x) { x.attacks[3].time_ms = 4000; }, "end exclusive window");
        bad([](auto& x) { x.attacks[1].time_ms = 999; }, "attack speed violation");
        bad([](auto& x) { x.attacks[0].hit = false; x.attacks[1].time_ms = 999; },
            "miss still consumes attack interval");
        bad([](auto& x) { x.attacks[1].time_ms = 0; }, "simultaneous distinct attacks");
        bad([](auto& x) { x.attacks[1].id = "aa1"; }, "duplicate event id");
        bad([](auto& x) { x.attacks.clear(); }, "empty attack list");
        bad([](auto& x) { x.critical_chance = 0; }, "critical impossible at probability zero");
        bad([](auto& x) { x.critical_chance = 1; }, "normal hit impossible at probability one");
        bad([](auto& x) { x.critical_chance = 1.1; }, "invalid probability");
        bad([](auto& x) { x.critical_multiplier = .5; }, "invalid critical multiplier");
        bad([](auto& x) { x.amplification_level_source = "character"; }, "wrong amplification level basis");
        bad([](auto& x) { x.amplification_levels = -1; }, "negative mastery");
        bad([](auto& x) { x.amplification_per_level = -.1; }, "negative mastery rate");
        bad([](auto& x) { x.attacks[0].observed_damage = -1; }, "negative observation");
        bad([](auto& x) { x.attack_speed = std::numeric_limits<double>::quiet_NaN(); }, "NaN");
        bad([](auto& x) { x.attacker.attack_power.flat.push_back({"fixture-weapon", 1}); },
            "duplicate contribution source");
        bad([](auto& x) { x.attacker.attack_power.base = std::numeric_limits<double>::max(); },
            "nonfinite damage rejected");
        std::cout << "PASS " << checks << " core checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
