#include "er/loadout.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string_view>

namespace er {
namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::invalid_argument(message);
}

std::string label(const CatalogRecord& record) {
    return record.value("_name", record.value("name", record.table)) + " [" + record.require("code") + "]";
}

std::string lower(std::string text) {
    for (auto& ch : text) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return text;
}

void append(std::string& text, const std::string& part) {
    if (!text.empty()) text += ", ";
    text += part;
}

void nonnegative(double value, const std::string& name) {
    require(std::isfinite(value) && value >= 0, name + ": 유한한 0 이상의 수치가 필요합니다.");
}

bool nonzero(const CatalogRecord& record, const std::string& key) {
    return record.fields.contains(key) && record.number(key) != 0;
}

void inspect_unimplemented_fields(const CatalogRecord& record, const std::string& context,
                                  ResolvedExperiment& out) {
    for (const auto& [field, value] : record.fields) {
        static_cast<void>(value);
        const auto key = lower(field);
        const bool damage = key.find("penetration") != std::string::npos ||
            key.find("adaptive") != std::string::npos ||
            key.find("increasebasicattack") != std::string::npos ||
            key.find("preventbasicattack") != std::string::npos ||
            key.find("preventcritical") != std::string::npos ||
            key.find("damageamplif") != std::string::npos ||
            key.find("damagereduct") != std::string::npos;
        const bool growth = key.find("bylv") != std::string::npos || key.find("bylevel") != std::string::npos;
        if ((damage || growth) && nonzero(record, field)) {
            out.blockers.push_back(context + ": " + field + "=" + record.require(field) + " 적용 규칙을 아직 지원하지 않습니다.");
            continue;
        }
        const bool disclosed = key.find("attackspeed") != std::string::npos ||
            key.find("criticalstrike") != std::string::npos || key.find("movespeed") != std::string::npos ||
            key.find("cooldown") != std::string::npos || key.find("hpregen") != std::string::npos ||
            key.find("lifesteal") != std::string::npos || key.find("skillamp") != std::string::npos ||
            key.find("preventskill") != std::string::npos || key.find("heal") != std::string::npos;
        if (disclosed && nonzero(record, field)) {
            out.notices.push_back(context + ": " + field + "=" + record.require(field) +
                " 원본값을 보존하며 단일 기본 공격 피해에 자동 적용하지 않습니다. 최종 공격 속도·치명타 수치는 별도 입력합니다.");
        }
    }
}

std::set<std::string> restriction_codes(std::string_view text) {
    std::set<std::string> result;
    std::size_t index = 0;
    while (index < text.size()) {
        if (text[index] == '[' || text[index] == ']' || text[index] == ',' || text[index] == ' ') { ++index; continue; }
        const auto start = index;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') ++index;
        require(index > start, "장비 itemUsableValueList의 실험체 ID 형식이 올바르지 않습니다.");
        const auto code = std::string(text.substr(start, index - start));
        if (code != "0") result.insert(code);
    }
    return result;
}

void validate_item_use(const CatalogRecord& item, const CatalogRecord& character, ResolvedExperiment& out,
                       const std::string& side) {
    const auto mode = lower(item.require("itemUsableType"));
    const auto context = side + " " + label(item);
    if (mode == "all" || mode == "weapontypeavailablecharacter") return;
    if (mode != "only" && mode != "except") {
        out.blockers.push_back(context + ": 알 수 없는 장비 제한 " + item.require("itemUsableType"));
        return;
    }
    const auto codes = restriction_codes(item.require("itemUsableValueList"));
    const bool listed = codes.contains(character.require("code"));
    require((mode == "only" && listed) || (mode == "except" && !listed), context + ": 이 실험체가 착용할 수 없는 장비입니다.");
}

void add_item(const CatalogRecord& item, const CatalogRecord& character, Combatant& actor,
              ResolvedExperiment& out, const std::string& side) {
    validate_item_use(item, character, out, side);
    const auto source = item.table + ":" + item.require("code");
    actor.attack_power.flat.push_back({source, item.number("attackPower")});
    actor.defense.flat.push_back({source, item.number("defense")});
    actor.max_hp.flat.push_back({source, item.number("maxHp")});
    append(actor.equipment, label(item));
    const auto& grade = item.require("itemGrade");
    if (grade != "Common" && grade != "Uncommon" && grade != "Rare") {
        out.blockers.push_back(side + " " + label(item) + ": " + grade +
            " 장비의 고유·조건부 효과 데이터가 없어 고정 스탯만 표시합니다. 피해 비교는 지원하지 않습니다.");
    }
    inspect_unimplemented_fields(item, side + " " + label(item), out);
}

void inspect_mode(const Catalog& catalog, const std::string& code, const std::string& weapon_type,
                  const std::string& side, ResolvedExperiment& out) {
    bool found = false;
    for (const auto* row : catalog.table("CharacterModeModifier")) {
        if (row->require("characterCode") != code) continue;
        const auto& type = row->require("weaponType");
        if (type != "None" && type != weapon_type) continue;
        found = true;
        for (const auto* field : {"squadIncreaseModeDamageRatio", "squadPreventModeDamageRatio"}) {
            if (row->number(field) != 0) out.blockers.push_back(side + ": " + field + "=" + row->require(field) + " 모드 보정을 아직 지원하지 않습니다.");
        }
    }
    if (!found) out.blockers.push_back(side + ": 스쿼드 모드 피해 보정 데이터가 없습니다.");
}

void validate_mastery_level(const Catalog& catalog, const std::string& type, int level,
                            const std::string& side, ResolvedExperiment& out) {
    const auto levels = catalog.table("MasteryLevel");
    if (levels.empty()) {
        out.blockers.push_back(side + ": 무기 숙련도 레벨 범위 데이터가 없습니다.");
        return;
    }
    const auto expected = std::to_string(level);
    const bool found = std::any_of(levels.begin(), levels.end(), [&](const auto* row) {
        return row->require("type") == type && row->require("masteryLevel") == expected;
    });
    require(found, side + ": 해당 무기 숙련도 레벨이 공식 데이터에 없습니다.");
}

double mastery_rate(const Catalog& catalog, const std::string& code, const std::string& type,
                    const std::string& side, ResolvedExperiment& out) {
    const CatalogRecord* selected = nullptr;
    for (const auto* row : catalog.table("MasteryStat")) {
        if (row->require("characterCode") == code && row->require("type") == type) {
            require(!selected, side + ": 중복된 실험체·무기 숙련도 정의입니다.");
            selected = row;
        }
    }
    if (!selected) {
        out.blockers.push_back(side + ": 실험체·무기의 숙련도 효과 데이터가 없습니다.");
        return 0;
    }
    std::optional<double> damage_rate;
    for (const auto* ordinal : {"first", "second", "third"}) {
        const auto option = std::string(ordinal) + "Option";
        const auto& kind = selected->require(option);
        const double rate = selected->number(option + "Section1Value");
        bool constant = true;
        for (int section = 2; section <= 4; ++section) {
            if (selected->number(option + "Section" + std::to_string(section) + "Value") != rate) constant = false;
        }
        if (!constant) {
            out.blockers.push_back(side + ": 숙련도 " + kind + "의 레벨 구간 적용 규칙을 아직 지원하지 않습니다.");
            continue;
        }
        if (kind == "IncreaseBasicAttackDamageRatio") {
            require(!damage_rate, side + ": 중복된 기본 공격 숙련도 증폭 정의입니다.");
            nonnegative(rate, side + " 숙련도 기본 공격 증폭률");
            damage_rate = rate;
        } else if (kind == "AttackSpeedRatio") {
            out.notices.push_back(side + ": 숙련도 AttackSpeedRatio=" + selected->require(option + "Section1Value") +
                " 원본값을 확인했습니다. 최종 공격 속도의 합산식은 자동 적용하지 않습니다.");
        } else if (kind != "None" || rate != 0) {
            out.blockers.push_back(side + ": 숙련도 효과 " + kind + "를 아직 지원하지 않습니다.");
        }
    }
    if (!damage_rate) out.blockers.push_back(side + ": 기본 공격 숙련도 증폭률을 확정할 수 없습니다.");
    return damage_rate.value_or(0);
}

struct ResolvedActor {
    Combatant combatant;
    double amplification_per_level{};
};

Combatant resolve_dummy(double max_hp, double defense) {
    require(std::isfinite(max_hp) && max_hp > 0, "더미 최대 체력은 유한한 양수여야 합니다.");
    nonnegative(defense, "더미 방어력");
    Combatant actor;
    actor.character_id = "더미 [test-dummy]";
    actor.weapon_id = "none";
    actor.equipment = "none";
    actor.traits = "none";
    actor.masteries = "none";
    actor.skill_levels = "none";
    actor.initial_effects = "none";
    // Internal level one supplies the existing core model with zero growth.
    actor.level = 1;
    actor.attack_power = {0, 0, {}};
    actor.defense = {defense, 0, {}};
    actor.max_hp = {max_hp, 0, {}};
    return actor;
}

ResolvedActor resolve_actor(const Catalog& catalog, const NamedLoadout& input,
                            const std::string& side, ResolvedExperiment& out) {
    require(input.level >= 1, side + ": 실험체 레벨은 1 이상이어야 합니다.");
    require(input.weapon_mastery >= 1, side + ": 무기 숙련도 레벨은 1 이상이어야 합니다.");
    const auto& character = catalog.find("Character", input.character);
    const auto& code = character.require("code");
    const auto& growth = catalog.find("CharacterLevelUpStat", code);
    const auto& weapon = catalog.find("ItemWeapon", input.weapon);
    const auto& weapon_type = weapon.require("weaponType");
    const auto& allowed = catalog.find("CharacterMastery", code);
    bool compatible = false;
    for (int i = 1; i <= 4; ++i) if (allowed.require("weapon" + std::to_string(i)) == weapon_type) compatible = true;
    require(compatible && weapon_type != "None", side + ": 선택한 무기 종류를 이 실험체가 사용할 수 없습니다.");

    ResolvedActor resolved;
    auto& actor = resolved.combatant;
    actor.character_id = label(character);
    actor.weapon_id = label(weapon);
    actor.level = input.level;
    actor.masteries = weapon_type + "=" + std::to_string(input.weapon_mastery) + "; other=not-applied";
    actor.skill_levels = "none (basic attack only; passive inactive)";
    actor.initial_effects = "none";
    actor.attack_power = {character.number("attackPower"), growth.number("attackPower"), {}};
    actor.defense = {character.number("defense"), growth.number("defense"), {}};
    actor.max_hp = {character.number("maxHp"), growth.number("maxHp"), {}};
    inspect_unimplemented_fields(character, side + " " + label(character), out);
    inspect_unimplemented_fields(growth, side + " 레벨 성장", out);
    add_item(weapon, character, actor, out, side);
    std::set<std::string> slots;
    for (const auto& name : input.armor) {
        const auto& armor = catalog.find("ItemArmor", name);
        const auto& slot = armor.require("armorType");
        require(slots.insert(slot).second, side + ": 중복된 방어구 슬롯 " + slot);
        add_item(armor, character, actor, out, side);
    }
    std::set<std::string> trait_codes;
    for (const auto& name : input.traits) {
        const auto& trait = catalog.find("Trait", name);
        require(trait.require("active") == "true", side + ": 비활성 특성은 선택할 수 없습니다.");
        require(trait_codes.insert(trait.require("code")).second, side + ": 중복된 특성 " + label(trait));
        append(actor.traits, label(trait));
        out.blockers.push_back(side + " 특성 " + label(trait) + ": 효과 엔진이 구현되지 않아 피해 비교를 지원하지 않습니다.");
        if (!trait.value("_description").empty()) out.notices.push_back(side + " 특성 " + label(trait) + ": " + trait.require("_description"));
    }
    if (actor.traits.empty()) actor.traits = "none";
    if (code == "11") {
        require(input.yuki_buttons >= 0 && input.yuki_buttons <= character.number("maxExtraPoint"), side + ": 유키 단추 수가 데이터 범위를 벗어났습니다.");
        actor.initial_effects = "Yuki.buttons=" + std::to_string(input.yuki_buttons);
        if (input.yuki_buttons != 0) out.blockers.push_back(side + ": 유키 단추 추가 피해를 아직 지원하지 않습니다. 단추 0 상태로 비교하세요.");
        else out.notices.push_back(side + ": 유키 단추 0개로 시작합니다. 게임에서도 단추를 모두 소모하고 비교하세요.");
        if (weapon_type != "TwoHandSword") out.blockers.push_back(side + ": 유키 쌍검의 기본 공격 계수·다단 타격을 아직 지원하지 않습니다.");
    } else {
        require(input.yuki_buttons == 0, side + ": 유키가 아닌 실험체에 단추를 설정할 수 없습니다.");
        out.blockers.push_back(side + " " + label(character) + ": 기본 공격·패시브 규칙을 아직 검증하지 않았습니다. 스탯만 표시합니다.");
    }
    validate_mastery_level(catalog, weapon_type, input.weapon_mastery, side, out);
    resolved.amplification_per_level = mastery_rate(catalog, code, weapon_type, side, out);
    inspect_mode(catalog, code, weapon_type, side, out);
    static_cast<void>(calculate_permanent_stats(actor));
    return resolved;
}

} // namespace

ResolvedExperiment resolve_experiment(const Catalog& catalog, const NamedExperiment& input) {
    ResolvedExperiment out;
    out.defender_kind = input.defender_kind;
    auto& scenario = out.scenario;
    scenario.id = "named-basic-attack-experiment";
    scenario.patch_version = catalog.metadata().require("patch_version");
    scenario.data_version = catalog.metadata().require("data_version");
    scenario.data_source = catalog.metadata().require("source");
    scenario.formula_version = "basic-attack-experiment-v1";
    scenario.game_mode = "squad";
    scenario.notes = "공식 데이터 스냅샷 + 사용자 승인 임시 기본 공격 모델; 캐릭터·장비 고정 스탯과 무기 숙련도 증폭만 적용";
    auto attacker = resolve_actor(catalog, input.attacker, "공격자", out);
    scenario.attacker = std::move(attacker.combatant);
    if (input.defender_kind == DefenderKind::Dummy) {
        scenario.defender = resolve_dummy(input.dummy_max_hp, input.dummy_defense);
        out.notices.push_back("방어자 test-dummy: 사용자가 지정한 실험용 더미입니다. 체력·방어력만 사용하며 실험체 API 데이터, 성장, 장비, 특성, 숙련도, 모드 보정, 패시브를 적용하지 않습니다.");
    } else {
        require(input.defender_kind == DefenderKind::Character, "알 수 없는 방어자 종류입니다.");
        scenario.defender = resolve_actor(catalog, input.defender, "방어자", out).combatant;
    }
    scenario.amplification_level_source = "weapon_mastery";
    scenario.amplification_levels = input.attacker.weapon_mastery;
    scenario.amplification_per_level = attacker.amplification_per_level;
    scenario.duration_ms = input.duration_ms;
    scenario.attacks = input.attacks;
    scenario.attack_speed_known = input.final_attack_speed.has_value();
    scenario.attack_speed = input.final_attack_speed.value_or(0);
    scenario.critical_chance_known = input.final_critical_chance.has_value();
    scenario.critical_chance = input.final_critical_chance.value_or(0);
    scenario.critical_multiplier_known = input.final_critical_multiplier.has_value();
    scenario.critical_multiplier = input.final_critical_multiplier.value_or(0);
    if (input.final_attack_speed) require(std::isfinite(*input.final_attack_speed) && *input.final_attack_speed > 0, "최종 공격 속도는 유한한 양수여야 합니다.");
    if (input.final_critical_chance) require(std::isfinite(*input.final_critical_chance) && *input.final_critical_chance >= 0 && *input.final_critical_chance <= 1, "최종 치명타 확률은 0..1 비율이어야 합니다.");
    if (input.final_critical_multiplier) require(std::isfinite(*input.final_critical_multiplier) && *input.final_critical_multiplier >= 1, "최종 치명타 배율은 1 이상이어야 합니다.");
    if (!scenario.attack_speed_known) {
        out.notices.push_back("최종 공격 속도 미입력: 한 번의 기본 공격만 비교하며, 공격 속도 합산식을 추측하지 않습니다.");
        if (scenario.attacks.size() != 1) out.blockers.push_back("여러 기본 공격의 시간 간격 검증에는 최종 공격 속도 입력이 필요합니다.");
    }
    if (!scenario.critical_chance_known) out.notices.push_back("최종 치명타 확률 미입력: 확률은 미확정으로 표시하고, 입력한 일반/치명타 발생 결과만 비교합니다.");
    if (!scenario.critical_multiplier_known) {
        out.notices.push_back("최종 치명타 배율 미입력: 기본값을 추측하지 않습니다. 일반 기본 공격만 비교할 수 있습니다.");
        for (const auto& attack : scenario.attacks) if (attack.hit && attack.critical) out.blockers.push_back(attack.id + ": 치명타 적중 계산에는 최종 치명타 배율 입력이 필요합니다.");
    }
    const auto defender_stats = calculate_permanent_stats(scenario.defender);
    scenario.initial_hp = input.defender_initial_hp.value_or(defender_stats.max_hp.total);
    scenario.initial_shield = input.defender_initial_shield;
    nonnegative(scenario.initial_hp, "방어자 시작 체력");
    nonnegative(scenario.initial_shield, "방어자 시작 보호막");
    require(scenario.initial_hp <= defender_stats.max_hp.total, "방어자 시작 체력이 최대 체력을 초과합니다.");
    scenario.unsupported_effects = !out.blockers.empty();
    // Preserve raw field disclosures in exported experiment provenance.
    for (const auto& notice : out.notices) scenario.notes += "\n" + notice;
    return out;
}

} // namespace er
