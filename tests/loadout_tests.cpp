#include "er/loadout.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
int failures = 0;
void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
template <typename Action> void rejects(Action action, const std::string& label) {
    try { action(); check(false, label); } catch (const std::exception&) { check(true, label); }
}
bool contains(const std::vector<std::string>& messages, const std::string& query) {
    for (const auto& message : messages) if (message.find(query) != std::string::npos) return true;
    return false;
}
void add(std::string& text, const std::string& table, const std::string& row,
         const std::string& key, const std::string& value) {
    text += table + '\t' + row + '\t' + key + '\t' + value + '\n';
}
std::string fixture() {
    // Synthetic numeric balance, with official identifier structure only.
    std::string text = "# er-catalog-v1\n";
    add(text, "Meta", "0", "data_version", "loadout-fixture-v1");
    add(text, "Meta", "0", "patch_version", "synthetic");
    add(text, "Meta", "0", "source", "offline synthetic fixture");
    for (const auto* code : {"11", "12"}) {
        add(text, "Character", code, "code", code);
        add(text, "Character", code, "_name", std::string(code) == "11" ? "유키" : "다른 실험체");
        add(text, "Character", code, "attackPower", "10");
        add(text, "Character", code, "defense", "20");
        add(text, "Character", code, "maxHp", "100");
        add(text, "Character", code, "maxExtraPoint", "4");
        add(text, "CharacterLevelUpStat", code, "code", code);
        add(text, "CharacterLevelUpStat", code, "attackPower", "2");
        add(text, "CharacterLevelUpStat", code, "defense", "3");
        add(text, "CharacterLevelUpStat", code, "maxHp", "4");
        add(text, "CharacterMastery", code, "code", code);
        add(text, "CharacterMastery", code, "weapon1", "TwoHandSword");
        add(text, "CharacterMastery", code, "weapon2", "DualSword");
        add(text, "CharacterMastery", code, "weapon3", "None");
        add(text, "CharacterMastery", code, "weapon4", "None");
        add(text, "CharacterModeModifier", code, "characterCode", code);
        add(text, "CharacterModeModifier", code, "weaponType", "TwoHandSword");
        add(text, "CharacterModeModifier", code, "squadIncreaseModeDamageRatio", "0");
        add(text, "CharacterModeModifier", code, "squadPreventModeDamageRatio", "0");
        add(text, "MasteryStat", code, "code", "mastery-" + std::string(code));
        add(text, "MasteryStat", code, "characterCode", code);
        add(text, "MasteryStat", code, "type", "TwoHandSword");
        add(text, "MasteryStat", code, "firstOption", "AttackSpeedRatio");
        add(text, "MasteryStat", code, "secondOption", "IncreaseBasicAttackDamageRatio");
        add(text, "MasteryStat", code, "thirdOption", "None");
        for (int section = 1; section <= 4; ++section) {
            const auto tail = "OptionSection" + std::to_string(section) + "Value";
            add(text, "MasteryStat", code, "first" + tail, "0.03");
            add(text, "MasteryStat", code, "second" + tail, "0.02");
            add(text, "MasteryStat", code, "third" + tail, "0");
        }
    }
    for (const auto* code : {"102101", "102102", "102103"}) {
        add(text, "ItemWeapon", code, "code", code);
        add(text, "ItemWeapon", code, "_name", std::string(code) == "102101" ? "녹슨 검" : "검" + std::string(code));
        add(text, "ItemWeapon", code, "weaponType", std::string(code) == "102103" ? "Glove" : "TwoHandSword");
        add(text, "ItemWeapon", code, "itemGrade", std::string(code) == "102102" ? "Epic" : "Common");
        add(text, "ItemWeapon", code, "itemUsableType", "WeaponTypeAvailableCharacter");
        add(text, "ItemWeapon", code, "itemUsableValueList", "0");
        add(text, "ItemWeapon", code, "attackPower", "5");
        add(text, "ItemWeapon", code, "defense", "0");
        add(text, "ItemWeapon", code, "maxHp", "0");
    }
    for (const auto* code : {"201101", "201102", "202101"}) {
        add(text, "ItemArmor", code, "code", code);
        add(text, "ItemArmor", code, "_name", "방어구" + std::string(code));
        add(text, "ItemArmor", code, "armorType", std::string(code) == "202101" ? "Chest" : "Head");
        add(text, "ItemArmor", code, "itemGrade", "Common");
        add(text, "ItemArmor", code, "itemUsableType", "ALL");
        add(text, "ItemArmor", code, "itemUsableValueList", "0");
        add(text, "ItemArmor", code, "attackPower", "0");
        add(text, "ItemArmor", code, "defense", "2");
        add(text, "ItemArmor", code, "maxHp", "10");
    }
    add(text, "Trait", "0", "code", "7001");
    add(text, "Trait", "0", "_name", "실험 특성");
    add(text, "Trait", "0", "_description", "효과 계산을 지원하지 않는 합성 특성");
    add(text, "Trait", "0", "active", "true");
    for (int level = 1; level <= 10; ++level) {
        add(text, "MasteryLevel", std::to_string(level), "type", "TwoHandSword");
        add(text, "MasteryLevel", std::to_string(level), "masteryLevel", std::to_string(level));
    }
    return text;
}
std::string replace(std::string text, const std::string& before, const std::string& after) {
    const auto position = text.find(before);
    if (position == std::string::npos) throw std::logic_error("fixture replacement missing");
    text.replace(position, before.size(), after);
    return text;
}
er::Catalog parse(const std::string& text) { std::istringstream stream(text); return er::Catalog::read(stream); }
} // namespace

int main() {
    try {
        const auto raw = fixture();
        const auto catalog = parse(raw);
        const er::NamedExperiment defaults;
        const auto resolved = er::resolve_experiment(catalog, defaults);
        check(defaults.defender_kind == er::DefenderKind::Dummy &&
              resolved.defender_kind == er::DefenderKind::Dummy, "dummy is the default defender kind");
        check(resolved.blockers.empty() && !resolved.scenario.unsupported_effects, "default one-hit supported");
        check(resolved.scenario.attacker.character_id.find("11") != std::string::npos &&
              resolved.scenario.attacker.character_id.find("유키") != std::string::npos, "name and code recorded");
        check(resolved.scenario.attacker.weapon_id.find("102101") != std::string::npos &&
              resolved.scenario.attacker.weapon_id.find("녹슨 검") != std::string::npos, "weapon name and code recorded");
        check(resolved.scenario.patch_version == "synthetic" && resolved.scenario.data_version == "loadout-fixture-v1",
              "catalog versions used");
        check(resolved.scenario.data_source == "offline synthetic fixture", "catalog source retained");
        check(resolved.scenario.formula_version == "basic-attack-experiment-v1", "approved formula unchanged");
        check(resolved.scenario.amplification_levels == 1 && resolved.scenario.amplification_per_level == 0.02,
              "mastery source resolved");
        check(!resolved.scenario.attack_speed_known && !resolved.scenario.critical_chance_known &&
              !resolved.scenario.critical_multiplier_known, "unknown final stats remain unknown");
        check(contains(resolved.notices, "단추") && contains(resolved.notices, "공격 속도") &&
              contains(resolved.notices, "치명타"), "unresolved scope notices");
        const auto result = er::simulate(resolved.scenario);
        check(result.attacker.attack_power.total == 15 && result.defender.defense.total == 0,
              "core computes character and equipment stats");
        check(std::abs(result.total_damage - 15.3) < 1e-12 &&
              result.impacts.front().raw_damage == result.impacts.front().total_damage,
              "zero-defense dummy takes full raw attack damage");
        check(resolved.scenario.initial_hp == 1000 && result.defender.max_hp.total == 1000 &&
              result.remaining_hp == 1000 - result.total_damage && resolved.scenario.initial_shield == 0,
              "dummy starts with 1000 health and no shield");
        const auto& dummy = resolved.scenario.defender;
        check(dummy.character_id == "더미 [test-dummy]" && dummy.weapon_id == "none" &&
              dummy.equipment == "none" && dummy.traits == "none" && dummy.masteries == "none" &&
              dummy.skill_levels == "none" && dummy.initial_effects == "none", "dummy has no character effects or loadout");
        check(dummy.attack_power.base == 0 && dummy.attack_power.per_level == 0 && dummy.attack_power.flat.empty() &&
              dummy.defense.per_level == 0 && dummy.defense.flat.empty() &&
              dummy.max_hp.per_level == 0 && dummy.max_hp.flat.empty(), "dummy has no attacks, growth or equipment contributions");
        check(resolved.scenario.notes.find("test-dummy") != std::string::npos,
              "dummy user-specified provenance retained");
        check(resolved.scenario.attacker.attack_power.flat.front().source_id.find("102101") != std::string::npos,
              "item official source ID retained");

        auto edited = defaults;
        edited.defender.character = "no-character-data";
        edited.defender.weapon = "no-weapon-data";
        edited.defender.armor = {"no-armor-data"};
        edited.defender.traits = {"no-trait-data"};
        edited.defender.level = 0;
        edited.defender.weapon_mastery = -1;
        edited.defender.yuki_buttons = -1;
        const auto isolated_dummy = er::resolve_experiment(catalog, edited);
        check(isolated_dummy.blockers.empty() &&
              er::simulate(isolated_dummy.scenario).total_damage == result.total_damage,
              "dummy bypasses inactive defender character, equipment, mastery and passive input");
        edited = defaults;
        edited.dummy_max_hp = 250;
        edited.dummy_defense = 100;
        const auto custom_dummy = er::resolve_experiment(catalog, edited);
        const auto custom_result = er::simulate(custom_dummy.scenario);
        check(custom_result.defender.max_hp.total == 250 && custom_result.defender.defense.total == 100 &&
              custom_dummy.scenario.initial_hp == 250, "custom dummy stats remain direct inputs");
        check(custom_result.total_damage == result.total_damage / 2,
              "custom dummy defense uses existing core formula");
        edited.defender_initial_hp = 80;
        edited.defender_initial_shield = 10;
        const auto custom_runtime = er::resolve_experiment(catalog, edited);
        check(custom_runtime.scenario.initial_hp == 80 && custom_runtime.scenario.initial_shield == 10,
              "custom dummy permits explicit runtime health and shield");
        edited.defender_initial_hp = 251;
        rejects([&] { er::resolve_experiment(catalog, edited); }, "custom dummy rejects health over its maximum");
        for (const auto invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            edited = defaults;
            edited.dummy_max_hp = invalid;
            rejects([&] { er::resolve_experiment(catalog, edited); }, "dummy maximum health must be finite and positive");
        }
        for (const auto invalid : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            edited = defaults;
            edited.dummy_defense = invalid;
            rejects([&] { er::resolve_experiment(catalog, edited); }, "dummy defense must be finite and nonnegative");
        }
        edited = defaults;
        edited.defender_kind = er::DefenderKind::Character;
        edited.dummy_max_hp = -1;
        edited.dummy_defense = -1;
        const auto legacy_character = er::resolve_experiment(catalog, edited);
        check(std::abs(er::simulate(legacy_character.scenario).total_damage - 12.75) < 1e-12 &&
              legacy_character.scenario.initial_hp == 100,
              "character defender retains previous damage and health independently of inactive dummy settings");

        edited = defaults;
        edited.attacker.level = 3;
        edited.attacker.weapon_mastery = 10;
        edited.defender_kind = er::DefenderKind::Character;
        edited.defender.level = 2;
        edited.defender.armor = {"201101", "방어구202101"};
        const auto equipped = er::resolve_experiment(catalog, edited);
        const auto equipment_result = er::simulate(equipped.scenario);
        check(equipment_result.attacker.attack_power.total == 19, "character level growth delegated");
        check(equipment_result.defender.defense.total == 27 && equipment_result.defender.max_hp.total == 124,
              "multiple named armor contributions");
        check(equipped.scenario.initial_hp == 124, "equipped defender health used");
        check(equipped.defender_kind == er::DefenderKind::Character &&
              equipped.scenario.defender.character_id.find("유키") != std::string::npos,
              "explicit character defender preserves character identity");
        check(equipped.scenario.amplification_levels == 10, "weapon mastery independent of character level");
        check(er::simulate(resolved.scenario).total_damage == result.total_damage, "independent resolution and runtime");

        edited = defaults;
        edited.attacker.traits = {"실험 특성"};
        const auto trait = er::resolve_experiment(catalog, edited);
        check(contains(trait.blockers, "특성") && trait.scenario.unsupported_effects, "traits explicitly block unsupported effects");
        check(trait.scenario.attacker.traits.find("7001") != std::string::npos &&
              trait.scenario.attacker.traits.find("실험 특성") != std::string::npos, "trait identity retained");
        rejects([&] { er::simulate(trait.scenario); }, "unsupported trait cannot silently calculate");
        edited.attacker.traits = {"missing"};
        rejects([&] { er::resolve_experiment(catalog, edited); }, "unknown trait rejected");
        edited = defaults;
        edited.attacker.weapon = "102102";
        check(!er::resolve_experiment(catalog, edited).blockers.empty(), "high-grade proc data missing blocks");
        edited.attacker.weapon = "102103";
        rejects([&] { er::resolve_experiment(catalog, edited); }, "incompatible weapon rejected");
        edited = defaults;
        edited.attacker.armor = {"201101", "201102"};
        rejects([&] { er::resolve_experiment(catalog, edited); }, "duplicate armor slot rejected");
        edited.attacker.armor = {"201101", "201101"};
        rejects([&] { er::resolve_experiment(catalog, edited); }, "duplicate armor item rejected");
        edited = defaults;
        edited.attacker.character = "없는 이름";
        rejects([&] { er::resolve_experiment(catalog, edited); }, "unknown character rejected");
        edited.attacker.character = "12";
        check(!er::resolve_experiment(catalog, edited).blockers.empty(), "unreviewed character cannot calculate");
        edited = defaults;
        edited.attacker.yuki_buttons = 1;
        check(contains(er::resolve_experiment(catalog, edited).blockers, "단추"), "passive button blocks");
        edited = defaults;
        edited.defender_kind = er::DefenderKind::Character;
        edited.defender.yuki_buttons = 1;
        check(contains(er::resolve_experiment(catalog, edited).blockers, "단추"), "defender passive state retained conservatively");
        edited.attacker.yuki_buttons = 5;
        rejects([&] { er::resolve_experiment(catalog, edited); }, "buttons above data maximum rejected");
        edited = defaults;
        edited.attacker.level = 0;
        rejects([&] { er::resolve_experiment(catalog, edited); }, "invalid character level rejected");
        edited = defaults;
        edited.attacker.weapon_mastery = -1;
        rejects([&] { er::resolve_experiment(catalog, edited); }, "invalid mastery level rejected");

        for (const auto* field : {"attackPowerByLv", "adaptiveForce", "penetrationDefense", "increaseBasicAttackDamageRatio", "preventCriticalStrikeDamaged"}) {
            auto altered = raw;
            add(altered, "ItemWeapon", "102101", field, "1");
            check(!er::resolve_experiment(parse(altered), defaults).blockers.empty(), std::string("unimplemented modifier blocked: ") + field);
        }
        auto altered = raw;
        add(altered, "ItemWeapon", "102101", "attackSpeedRatio", "0.2");
        const auto speed = er::resolve_experiment(parse(altered), defaults);
        check(speed.blockers.empty() && contains(speed.notices, "attackSpeedRatio"), "speed effect disclosed for one-hit experiment");
        altered = replace(raw, "MasteryStat\t11\tsecondOptionSection2Value\t0.02", "MasteryStat\t11\tsecondOptionSection2Value\t0.03");
        check(!er::resolve_experiment(parse(altered), defaults).blockers.empty(), "unknown mastery sections never guessed");
        altered = replace(raw, "CharacterModeModifier\t11\tsquadIncreaseModeDamageRatio\t0", "CharacterModeModifier\t11\tsquadIncreaseModeDamageRatio\t5");
        check(!er::resolve_experiment(parse(altered), defaults).blockers.empty(), "mode modifier unsupported");
        altered = replace(raw, "ItemArmor\t201101\titemUsableType\tALL", "ItemArmor\t201101\titemUsableType\tONLY");
        altered = replace(altered, "ItemArmor\t201101\titemUsableValueList\t0", "ItemArmor\t201101\titemUsableValueList\t51");
        edited = defaults;
        edited.attacker.armor = {"201101"};
        rejects([&] { er::resolve_experiment(parse(altered), edited); }, "character-restricted armor rejected");
        altered = replace(altered, "ItemArmor\t201101\titemUsableType\tONLY", "ItemArmor\t201101\titemUsableType\tEXCEPT");
        check(er::resolve_experiment(parse(altered), edited).blockers.empty(), "excluded other character permits equipment");
        altered = replace(altered, "ItemArmor\t201101\titemUsableValueList\t51", "ItemArmor\t201101\titemUsableValueList\t11");
        rejects([&] { er::resolve_experiment(parse(altered), edited); }, "excluded selected character rejected");

        edited = defaults;
        edited.attacks[0].critical = true;
        const auto missing_crit = er::resolve_experiment(catalog, edited);
        check(!missing_crit.blockers.empty(), "critical hit requires explicit multiplier");
        edited.final_critical_multiplier = 1.75;
        edited.final_critical_chance = 0.5;
        edited.final_attack_speed = 1.25;
        const auto explicit_stats = er::resolve_experiment(catalog, edited);
        check(explicit_stats.blockers.empty() && explicit_stats.scenario.attack_speed_known &&
              explicit_stats.scenario.critical_chance_known && explicit_stats.scenario.critical_multiplier_known,
              "explicit final stat overrides retained");
        check(std::abs(er::simulate(explicit_stats.scenario).total_damage - result.total_damage * 1.75) < 1e-12,
              "critical override passed to core");
        edited = defaults;
        edited.attacks.push_back({"aa2", 500, true, false, std::nullopt});
        check(!er::resolve_experiment(catalog, edited).blockers.empty(), "multiple attacks need known final speed");
        edited.final_attack_speed = 2;
        check(er::resolve_experiment(catalog, edited).blockers.empty(), "explicit speed permits timing validation");
        edited = defaults;
        edited.defender_initial_hp = 80;
        edited.defender_initial_shield = 10;
        const auto health = er::resolve_experiment(catalog, edited);
        check(health.scenario.initial_hp == 80 && health.scenario.initial_shield == 10, "explicit runtime state retained");
        edited.final_attack_speed = std::numeric_limits<double>::infinity();
        rejects([&] { er::resolve_experiment(catalog, edited); }, "nonfinite override rejected");
        edited = defaults;
        edited.final_critical_chance = 1.01;
        rejects([&] { er::resolve_experiment(catalog, edited); }, "critical chance outside fraction range rejected");
        edited = defaults;
        edited.final_critical_multiplier = 0.75;
        rejects([&] { er::resolve_experiment(catalog, edited); }, "critical multiplier below one rejected");
        edited = defaults;
        edited.defender_initial_hp = 1001;
        rejects([&] { er::resolve_experiment(catalog, edited); }, "initial health above max rejected");
        edited = defaults;
        edited.defender_initial_shield = -1;
        rejects([&] { er::resolve_experiment(catalog, edited); }, "negative initial shield rejected");
        edited = defaults;
        edited.attacker.weapon_mastery = 11;
        rejects([&] { er::resolve_experiment(catalog, edited); }, "mastery level absent from official table rejected");
        edited = defaults;
        edited.attacker.traits = {"7001", "실험 특성"};
        rejects([&] { er::resolve_experiment(catalog, edited); }, "same trait by ID and name rejected");
        edited.attacker.traits = {"7001"};
        altered = replace(raw, "Trait\t0\tactive\ttrue", "Trait\t0\tactive\tfalse");
        rejects([&] { er::resolve_experiment(parse(altered), edited); }, "inactive trait rejected");
        altered = replace(raw, "ItemWeapon\t102101\titemUsableType\tWeaponTypeAvailableCharacter", "ItemWeapon\t102101\titemUsableType\tOnly");
        altered = replace(altered, "ItemWeapon\t102101\titemUsableValueList\t0", "ItemWeapon\t102101\titemUsableValueList\t[11,51]");
        check(er::resolve_experiment(parse(altered), defaults).blockers.empty(), "structured character restriction list supported");
        altered = replace(altered, "ItemWeapon\t102101\titemUsableValueList\t[11,51]", "ItemWeapon\t102101\titemUsableValueList\t[44,51]");
        rejects([&] { er::resolve_experiment(parse(altered), defaults); }, "character-exclusive weapon rejected");
        altered = raw;
        std::size_t dual_position = 0;
        while ((dual_position = altered.find("TwoHandSword", dual_position)) != std::string::npos) {
            altered.replace(dual_position, 12, "DualSword");
            dual_position += 9;
        }
        const auto dual = er::resolve_experiment(parse(altered), defaults);
        check(contains(dual.blockers, "쌍검"), "dual-sword attacks cannot silently use single-hit coefficients");
        check(er::calculate_permanent_stats(dual.scenario.attacker).attack_power.total == 15,
              "unsupported dual-sword effects retain fixed-stat inspection");
        altered = replace(raw, "MasteryStat\t11\tsecondOption\tIncreaseBasicAttackDamageRatio", "MasteryStat\t11\tsecondOption\tUnknownEffect");
        check(!er::resolve_experiment(parse(altered), defaults).blockers.empty(), "unknown mastery effect does not default to zero");
        altered = replace(raw, "ItemWeapon\t102101\tattackPower\t5", "ItemWeapon\t102101\tattackPower\tnan");
        rejects([&] { er::resolve_experiment(parse(altered), defaults); }, "nonfinite source stat rejected");
    } catch (const std::exception& error) { check(false, std::string("unexpected exception: ") + error.what()); }
    std::cout << "loadout checks=" << checks << ", failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
