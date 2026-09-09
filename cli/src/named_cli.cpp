#include "er/named_cli.hpp"
#include "er/scenario_io.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace er {
namespace {
std::string trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == s.npos) return {};
    return std::string(s.substr(first, s.find_last_not_of(" \t\r") - first + 1));
}
void utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto lead = static_cast<unsigned char>(s[i]);
        if (lead < 128) {
            if ((lead < 32 && lead != '\t') || lead == 127) throw std::invalid_argument("invalid control character");
            ++i; continue;
        }
        const unsigned count = lead >= 0xc2 && lead <= 0xdf ? 2 : lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        if (!count || i + count > s.size()) throw std::invalid_argument("invalid UTF-8 input");
        unsigned cp = lead & (count == 2 ? 31 : count == 3 ? 15 : 7);
        for (unsigned n = 1; n < count; ++n) {
            const auto c = static_cast<unsigned char>(s[i + n]);
            if ((c & 0xc0) != 0x80) throw std::invalid_argument("invalid UTF-8 input");
            cp = (cp << 6) | (c & 63);
        }
        if (cp < (count == 2 ? 128u : count == 3 ? 2048u : 65536u) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            throw std::invalid_argument("invalid UTF-8 input");
        i += count;
    }
}
template<class T> T num(const std::string& text) {
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || ec != std::errc{} || end != text.data() + text.size()) throw std::invalid_argument("숫자 형식 오류: " + text);
    if constexpr (std::is_floating_point_v<T>) if (!std::isfinite(value)) throw std::invalid_argument("유한한 숫자가 필요합니다.");
    return value;
}
Attack attack_row(const std::string& value) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const auto end = value.find(',', start);
        parts.push_back(trim(std::string_view(value).substr(start, end == value.npos ? end : end - start)));
        if (end == value.npos) break;
        start = end + 1;
    }
    if (parts.size() != 5 || parts[0].empty()) throw std::invalid_argument("attack=id,time_ms,hit|miss,normal|critical,observed|na 형식이 필요합니다.");
    if (parts[2] != "hit" && parts[2] != "miss") throw std::invalid_argument("hit 또는 miss를 입력하세요.");
    if (parts[3] != "normal" && parts[3] != "critical") throw std::invalid_argument("normal 또는 critical을 입력하세요.");
    Attack attack{parts[0], num<std::int64_t>(parts[1]), parts[2] == "hit", parts[3] == "critical", {}};
    if (parts[4] != "na") attack.observed_damage = num<double>(parts[4]);
    return attack;
}
std::string table_name(const std::string& kind) {
    if (kind == "characters") return "Character";
    if (kind == "weapons") return "ItemWeapon";
    if (kind == "armor") return "ItemArmor";
    if (kind == "traits") return "Trait";
    throw std::invalid_argument("목록: characters, weapons, armor, traits");
}
std::string one_line(std::string text) {
    for (std::size_t p = 0; (p = text.find("\\n", p)) != text.npos;) text.replace(p, 2, " ");
    std::replace(text.begin(), text.end(), '\n', ' ');
    std::replace(text.begin(), text.end(), '\r', ' ');
    return text;
}
std::vector<const CatalogRecord*> choices(const Catalog& catalog, const std::string& kind, const std::string& query) {
    auto result = catalog.search(table_name(kind), query);
    std::erase_if(result, [&](const CatalogRecord* r) {
        return (kind == "traits" && r->value("active") != "true") ||
            ((kind == "weapons" || kind == "armor") &&
             (r->value("isRemoved", "false") == "true" || r->value("showInItemBook", "true") == "false"));
    });
    return result;
}
void show_record(std::ostream& out, const CatalogRecord& row) {
    out << row.require("_name") << " [" << row.require("code") << "]";
    for (const auto* key : {"itemGrade", "weaponType", "armorType", "traitGroup", "traitType"})
        if (!row.value(key).empty()) out << " / " << row.value(key);
    for (const auto* key : {"attackPower", "defense", "maxHp"})
        if (row.fields.contains(key) && row.number(key) != 0) out << " / " << key << '=' << row.value(key);
    out << '\n';
    const auto desc = row.value("_description");
    if (!desc.empty()) out << "   " << one_line(desc) << '\n';
}
std::string ask(std::istream& in, std::ostream& out, const std::string& label, const std::string& current = "") {
    out << label;
    if (!current.empty()) out << " [" << current << ']';
    out << ": " << std::flush;
    std::string line;
    if (!std::getline(in, line)) throw std::runtime_error("입력이 종료되었습니다.");
    line = trim(line);
    utf8(line);
    return line.empty() ? current : line;
}
std::string select(const Catalog& catalog, const std::string& kind, std::istream& in, std::ostream& out) {
    const auto query = ask(in, out, "이름 일부 또는 코드 (Enter: 취소, *: 전체)");
    if (query.empty()) return {};
    const auto rows = choices(catalog, kind, query == "*" ? "" : query);
    if (rows.empty()) { out << "일치하는 이름이 없습니다.\n"; return {}; }
    if (rows.size() > 40) { out << rows.size() << "개가 검색되었습니다. 이름을 더 입력해 범위를 좁혀 주세요.\n"; return {}; }
    for (std::size_t i = 0; i < rows.size(); ++i) { out << i + 1 << ". "; show_record(out, *rows[i]); }
    const auto selected = ask(in, out, "선택 번호 (0: 취소)", rows.size() == 1 ? "1" : "0");
    const auto n = num<std::size_t>(selected);
    if (!n) return {};
    if (n > rows.size()) throw std::invalid_argument("목록에 있는 번호를 선택하세요.");
    const auto& row = *rows[n - 1];
    try {
        // Keep saved inputs readable; official IDs disambiguate duplicate names.
        if (&catalog.find(row.table, row.require("_name")) == &row) return row.require("_name");
    } catch (const std::exception&) {}
    return row.require("code");
}
void edit_loadout(const Catalog& catalog, NamedLoadout& loadout, std::istream& in, std::ostream& out) {
    out << "1 실험체  2 무기  3 방어구 추가  4 방어구 비우기  5 특성 추가  6 특성 비우기  7 레벨·숙련도·단추\n";
    const auto action = ask(in, out, "수정 항목", "0");
    if (action == "1" || action == "2") {
        const auto value = select(catalog, action == "1" ? "characters" : "weapons", in, out);
        if (!value.empty()) (action == "1" ? loadout.character : loadout.weapon) = value;
    } else if (action == "3" || action == "5") {
        const auto value = select(catalog, action == "3" ? "armor" : "traits", in, out);
        if (!value.empty() && action == "3") {
            const auto slot = catalog.find("ItemArmor", value).require("armorType");
            const auto previous_count = loadout.armor.size();
            std::erase_if(loadout.armor, [&](const auto& armor) { return catalog.find("ItemArmor", armor).require("armorType") == slot; });
            if (loadout.armor.size() != previous_count) out << "같은 슬롯의 기존 방어구를 교체했습니다.\n";
            loadout.armor.push_back(value);
        } else if (!value.empty()) loadout.traits.push_back(value);
    } else if (action == "4") loadout.armor.clear();
    else if (action == "6") loadout.traits.clear();
    else if (action == "7") {
        loadout.level = num<int>(ask(in, out, "캐릭터 레벨", std::to_string(loadout.level)));
        loadout.weapon_mastery = num<int>(ask(in, out, "무기 숙련도 레벨", std::to_string(loadout.weapon_mastery)));
        loadout.yuki_buttons = num<int>(ask(in, out, "유키 남은 단추 (현재 피해 지원: 0)", std::to_string(loadout.yuki_buttons)));
    } else if (action != "0") throw std::invalid_argument("지원하지 않는 메뉴 번호입니다.");
}
std::string decimal(const std::optional<double>& value) {
    if (!value) return "na";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << *value;
    return out.str();
}
void optional_number(std::optional<double>& value, const std::string& key) {
    if (key == "na") value.reset(); else value = num<double>(key);
}
void show_loadout(std::ostream& out, const Catalog& catalog, const NamedLoadout& loadout) {
    const auto name = [&](const std::string& table, const std::string& value) {
        try { return catalog.find(table, value).require("_name"); }
        catch (const std::exception&) { return value; }
    };
    out << name("Character", loadout.character) << " Lv." << loadout.level << " / " << name("ItemWeapon", loadout.weapon)
        << " / 무기 숙련 " << loadout.weapon_mastery << " / 단추 " << loadout.yuki_buttons << '\n';
    out << "  방어구: ";
    if (loadout.armor.empty()) out << "없음";
    for (const auto& v : loadout.armor) out << name("ItemArmor", v) << "  ";
    out << "\n  특성: ";
    if (loadout.traits.empty()) out << "없음 (발동 효과 제외 실험)";
    for (const auto& v : loadout.traits) out << name("Trait", v) << "  ";
    out << '\n';
}
} // namespace

NamedExperiment read_named_experiment(std::istream& in) {
    NamedExperiment result;
    std::string line, section;
    std::set<std::string> seen;
    bool attacks_seen = false;
    bool defender_type_seen = false, defender_character_fields = false, defender_dummy_fields = false;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        if (line_number == 1 && line.starts_with("\xef\xbb\xbf")) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        try {
            utf8(line);
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            if (line[0] == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                if (section != "attacker" && section != "defender" && section != "experiment") throw std::invalid_argument("unknown section");
                continue;
            }
            const auto equal = line.find('=');
            if (section.empty() || equal == line.npos) throw std::invalid_argument("section과 key=value가 필요합니다.");
            const auto key = trim(std::string_view(line).substr(0, equal));
            const auto value = trim(std::string_view(line).substr(equal + 1));
            if (value.empty()) throw std::invalid_argument("빈 값은 허용되지 않습니다.");
            const bool repeated = section == "experiment" ? key == "attack" : key == "armor" || key == "trait";
            if (!repeated && !seen.insert(section + "." + key).second) throw std::invalid_argument("중복 key: " + key);
            if (section == "experiment") {
                if (key == "duration_ms") result.duration_ms = num<std::int64_t>(value);
                else if (key == "final_attack_speed") optional_number(result.final_attack_speed, value);
                else if (key == "final_critical_chance") optional_number(result.final_critical_chance, value);
                else if (key == "final_critical_multiplier") optional_number(result.final_critical_multiplier, value);
                else if (key == "defender_initial_hp") optional_number(result.defender_initial_hp, value);
                else if (key == "defender_initial_shield") result.defender_initial_shield = num<double>(value);
                else if (key == "attack") {
                    if (!attacks_seen) { result.attacks.clear(); attacks_seen = true; }
                    result.attacks.push_back(attack_row(value));
                } else throw std::invalid_argument("unknown key: " + key);
            } else if (section == "defender" && key == "type") {
                if (value != "dummy" && value != "character") throw std::invalid_argument("defender.type은 dummy 또는 character입니다.");
                result.defender_kind = value == "dummy" ? DefenderKind::Dummy : DefenderKind::Character;
                defender_type_seen = true;
            } else if (section == "defender" && (key == "max_hp" || key == "defense")) {
                (key == "max_hp" ? result.dummy_max_hp : result.dummy_defense) = num<double>(value);
                defender_dummy_fields = true;
            } else {
                auto& loadout = section == "attacker" ? result.attacker : result.defender;
                if (section == "defender") defender_character_fields = true;
                if (key == "character") loadout.character = value;
                else if (key == "weapon") loadout.weapon = value;
                else if (key == "level") loadout.level = num<int>(value);
                else if (key == "weapon_mastery") loadout.weapon_mastery = num<int>(value);
                else if (key == "yuki_buttons") loadout.yuki_buttons = num<int>(value);
                else if (key == "armor") loadout.armor.push_back(value);
                else if (key == "trait") loadout.traits.push_back(value);
                else throw std::invalid_argument("unknown key: " + key);
            }
        } catch (const std::exception& e) { throw std::invalid_argument("loadout line " + std::to_string(line_number) + ": " + e.what()); }
    }
    if (in.bad()) throw std::runtime_error("입력 파일 읽기 실패");
    // Older saved loadouts explicitly identify a character but have no type key.
    if (!defender_type_seen && defender_character_fields) result.defender_kind = DefenderKind::Character;
    if ((result.defender_kind == DefenderKind::Dummy && defender_character_fields) ||
        (result.defender_kind == DefenderKind::Character && defender_dummy_fields))
        throw std::invalid_argument("수비 더미의 max_hp/defense와 실험체의 장비·숙련도 설정을 혼합할 수 없습니다.");
    return result;
}

void write_named_experiment(std::ostream& out, const NamedExperiment& input) {
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    const auto write = [&](const char* section, const NamedLoadout& v) {
        out << '[' << section << "]\n";
        if (std::string_view(section) == "defender") out << "type=character\n";
        out << "character=" << v.character << "\nweapon=" << v.weapon << "\nlevel=" << v.level
            << "\nweapon_mastery=" << v.weapon_mastery << "\nyuki_buttons=" << v.yuki_buttons << '\n';
        for (const auto& armor : v.armor) out << "armor=" << armor << '\n';
        for (const auto& trait : v.traits) out << "trait=" << trait << '\n';
        out << '\n';
    };
    write("attacker", input.attacker);
    if (input.defender_kind == DefenderKind::Dummy)
        out << "[defender]\ntype=dummy\nmax_hp=" << input.dummy_max_hp << "\ndefense=" << input.dummy_defense << "\n\n";
    else write("defender", input.defender);
    out << "[experiment]\nduration_ms=" << input.duration_ms << "\nfinal_attack_speed=" << decimal(input.final_attack_speed)
        << "\nfinal_critical_chance=" << decimal(input.final_critical_chance)
        << "\nfinal_critical_multiplier=" << decimal(input.final_critical_multiplier)
        << "\ndefender_initial_hp=" << decimal(input.defender_initial_hp)
        << "\ndefender_initial_shield=" << input.defender_initial_shield << '\n';
    for (const auto& a : input.attacks) out << "attack=" << a.id << ',' << a.time_ms << ',' << (a.hit ? "hit" : "miss") << ','
        << (a.critical ? "critical" : "normal") << ',' << decimal(a.observed_damage) << '\n';
    if (!out) throw std::runtime_error("설정 파일 쓰기 실패");
}

void list_catalog(std::ostream& out, const Catalog& catalog, const std::string& kind, const std::string& query) {
    const auto rows = choices(catalog, kind, query);
    for (const auto* row : rows) show_record(out, *row);
    out << rows.size() << "개 / 데이터 " << catalog.metadata().require("data_version") << '\n';
}

void write_named_report(std::ostream& out, const ResolvedExperiment& resolved) {
    const auto& s = resolved.scenario;
    out.imbue(std::locale::classic());
    out << std::setprecision(10);
    out << "\n[기본 공격 실험 · 임시 계산 모델]\n데이터: " << s.data_version << " / 패치: " << s.patch_version << " / 공식: " << s.formula_version << '\n';
    const auto stats = [&](const char* who, const Combatant& combatant, bool dummy = false) {
        const auto v = calculate_permanent_stats(combatant);
        if (dummy) {
            out << who << ": 더미 / 최대 체력 " << v.max_hp.total << " / 방어력 " << v.defense.total
                << "\n  장비·특성·숙련도·패시브·모드 보정 없음\n";
            return;
        }
        out << who << ": " << combatant.character_id << " / " << combatant.weapon_id << " / Lv." << combatant.level << '\n'
            << "  장비: " << combatant.equipment << "\n  특성: " << (combatant.traits == "none" ? "없음 (발동 효과 제외)" : combatant.traits) << '\n';
        const auto row = [&](const char* label, const StatResult& n) {
            out << "  " << label << " = 기본 " << n.base << " + 레벨 성장 " << n.growth << " + 장비 고정 " << n.flat << " = " << n.total << '\n';
        };
        row("공격력", v.attack_power); row("방어력", v.defense); row("최대 체력", v.max_hp);
    };
    stats("공격자", s.attacker); stats("방어자", s.defender, resolved.defender_kind == DefenderKind::Dummy);
    out << "시작 체력 " << s.initial_hp << " / 시작 보호막 " << s.initial_shield << '\n';
    out << "최종 공격 속도: " << (s.attack_speed_known ? decimal(s.attack_speed) : "미입력")
        << " / 치명타 확률: " << (s.critical_chance_known ? decimal(s.critical_chance) : "미입력")
        << " / 치명타 배율: " << (s.critical_multiplier_known ? decimal(s.critical_multiplier) : "미입력") << '\n';
    if (!resolved.blockers.empty()) {
        out << "\n[피해 계산 불가 · 위 스탯은 지원되는 기초 항목만 합산한 값]\n";
        for (const auto& b : resolved.blockers) out << "- " << b << '\n';
    } else {
        const auto result = simulate(s);
        out << "\n평타 증폭 = 무기 숙련 " << s.amplification_levels << " × " << s.amplification_per_level * 100
            << "% = " << result.basic_attack_amplification * 100 << "%\n";
        out << "평타 = 공격력 × (1 + 평타 증폭) × 치명타 배율 × 100 / (100 + 방어력)\n";
        for (const auto& impact : result.impacts) {
            out << "  " << impact.id << " @" << impact.time_ms << "ms " << (impact.hit ? "적중" : "빗나감")
                << (impact.critical ? " / 치명타" : " / 일반") << ": 방어 전 " << impact.raw_damage << " → 피해 " << impact.total_damage;
            if (impact.comparison) out << " / 실측 " << impact.comparison->observed << " / 계산−실측 " << impact.comparison->delta;
            out << '\n';
        }
        out << "총 피해 " << result.total_damage << " / 측정 구간 " << s.duration_ms << "ms / 콤보 DPS " << result.combo_dps
            << "\n보호막 피해 " << result.shield_damage << " / 체력 피해 " << result.health_damage
            << "\n남은 체력 " << result.remaining_hp << " / 남은 보호막 " << result.remaining_shield << '\n';
        if (result.measured_comparison) out << "실측 " << result.measured_count << "회 합계 " << result.measured_comparison->observed
            << " / 같은 타격의 계산−실측 " << result.measured_comparison->delta << '\n';
    }
    out << "\n조건·미적용 항목:\n";
    out << "- 사용자 승인 임시 식입니다. 단추 0개·Q 미사용 상태에서 비교하세요.\n"
        << "- 무기 외 숙련도, 체력 재생, 버프·효과는 미적용입니다. 스탯 합산에는 기본값·레벨 성장·장비 고정값만 포함됩니다.\n"
        << "- 일반/치명타는 입력한 타격 결과를 사용합니다. 게임 표시 반올림과 동작 지연은 미검증이며, 사망 후에도 구간을 끝까지 계산합니다.\n";
    for (const auto& notice : resolved.notices)
        if (notice.find("원본값") == notice.npos && !notice.starts_with("최종") && notice.find("단추 0개") == notice.npos)
            out << "- " << one_line(notice) << '\n';
}

int interactive_experiment(const Catalog& catalog, NamedExperiment input,
                           const std::filesystem::path& save_directory, std::istream& in, std::ostream& out) {
    out << "유키 기본 공격 실험\nEnter로 기본 계산. 유키 단추 0개, Q 미사용, 장비·특성 발동 효과 없는 조건입니다.\n";
    for (;;) {
        out << "\n공격자 "; show_loadout(out, catalog, input.attacker);
        out << "방어자 ";
        if (input.defender_kind == DefenderKind::Dummy)
            out << "더미 / 최대 체력 " << input.dummy_max_hp << " / 방어력 " << input.dummy_defense << " / 장비·특성·숙련도 없음\n";
        else show_loadout(out, catalog, input.defender);
        out << "공격 " << input.attacks.size() << "회 / 구간 " << input.duration_ms << "ms\n"
            << "1 계산(Enter)  2 공격자 설정  3 방어자 설정  4 공격·실측값  5 최종 스탯·체력\n"
            << "6 이름 목록 검색  7 기본값 초기화  8 저장 설정 불러오기  9 설정·결과 저장  0 종료\n> " << std::flush;
        std::string action;
        if (!std::getline(in, action)) return 0;
        action = trim(action);
        if (action == "0") return 0;
        try {
            auto changed = input;
            if (action.empty() || action == "1") write_named_report(out, resolve_experiment(catalog, input));
            else if (action == "2") edit_loadout(catalog, changed.attacker, in, out);
            else if (action == "3") {
                const auto kind = ask(in, out, "수비 대상: 1 더미 체력·방어력 설정 / 2 실험체 설정 / 0 취소",
                                      input.defender_kind == DefenderKind::Dummy ? "1" : "2");
                if (kind == "1") {
                    changed.defender_kind = DefenderKind::Dummy;
                    changed.dummy_max_hp = num<double>(ask(in, out, "더미 최대 체력", decimal(input.dummy_max_hp)));
                    changed.dummy_defense = num<double>(ask(in, out, "더미 방어력", decimal(input.dummy_defense)));
                    if (changed.dummy_max_hp <= 0 || changed.dummy_defense < 0) throw std::invalid_argument("더미 체력은 양수, 방어력은 0 이상이어야 합니다.");
                    if (input.defender_kind != DefenderKind::Dummy || changed.dummy_max_hp != input.dummy_max_hp) {
                        changed.defender_initial_hp.reset();
                        out << "시작 체력을 더미 최대 체력으로 설정했습니다.\n";
                    }
                    if (input.defender_kind != DefenderKind::Dummy) changed.defender_initial_shield = 0;
                } else if (kind == "2") {
                    changed.defender_kind = DefenderKind::Character;
                    if (input.defender_kind != DefenderKind::Character) {
                        changed.defender_initial_hp.reset();
                        changed.defender_initial_shield = 0;
                    }
                    edit_loadout(catalog, changed.defender, in, out);
                } else if (kind != "0") throw std::invalid_argument("수비 대상 번호를 선택하세요.");
            }
            else if (action == "4") {
                changed.duration_ms = num<std::int64_t>(ask(in, out, "측정 구간(ms)", std::to_string(input.duration_ms)));
                const auto count = num<std::size_t>(ask(in, out, "공격 횟수", std::to_string(input.attacks.size())));
                if (!count || count > 1000) throw std::invalid_argument("공격 횟수는 1~1000 범위입니다.");
                changed.attacks.clear();
                out << "형식: ID,시각ms,hit 또는 miss,normal 또는 critical,실측 피해 또는 na\n";
                for (std::size_t i = 0; i < count; ++i) {
                    std::ostringstream row;
                    if (i < input.attacks.size()) {
                        const auto& a = input.attacks[i];
                        row << a.id << ',' << a.time_ms << ',' << (a.hit ? "hit" : "miss") << ',' << (a.critical ? "critical" : "normal") << ',' << decimal(a.observed_damage);
                    }
                    changed.attacks.push_back(attack_row(ask(in, out, "공격 " + std::to_string(i + 1), row.str())));
                }
            } else if (action == "5") {
                out << "모르는 값은 na. 연속 공격은 최종 공격 속도, 치명타 적중은 최종 치명타 배율이 필요합니다.\n";
                optional_number(changed.final_attack_speed, ask(in, out, "최종 공격 속도(회/초)", decimal(input.final_attack_speed)));
                optional_number(changed.final_critical_chance, ask(in, out, "최종 치명타 확률(0~1)", decimal(input.final_critical_chance)));
                optional_number(changed.final_critical_multiplier, ask(in, out, "최종 치명타 배율", decimal(input.final_critical_multiplier)));
                optional_number(changed.defender_initial_hp, ask(in, out, "방어자 시작 체력(na=계산된 최대 체력)", decimal(input.defender_initial_hp)));
                changed.defender_initial_shield = num<double>(ask(in, out, "방어자 시작 보호막", decimal(input.defender_initial_shield)));
            } else if (action == "6") {
                const auto kind = ask(in, out, "characters / weapons / armor / traits", "weapons");
                const auto query = ask(in, out, "검색어(Enter=전체)");
                list_catalog(out, catalog, kind, query);
            } else if (action == "7") changed = NamedExperiment{};
            else if (action == "8") {
                std::ifstream saved(save_directory / "yuki_test.ini", std::ios::binary);
                if (!saved) throw std::runtime_error("저장된 outputs/yuki_test.ini가 없습니다.");
                changed = read_named_experiment(saved);
            } else if (action == "9") {
                const auto resolved = resolve_experiment(catalog, input);
                std::ostringstream report, json;
                write_named_report(report, resolved);
                if (resolved.blockers.empty()) write_json(json, resolved.scenario, simulate(resolved.scenario));
                else json << "{\"status\":\"blocked\",\"details\":\"See yuki_test.txt; no damage result was calculated.\"}\n";
                std::filesystem::create_directories(save_directory);
                const auto save = [&](const char* name, const std::string& body) {
                    std::ofstream file(save_directory / name, std::ios::binary | std::ios::trunc);
                    file << body;
                    file.close();
                    if (!file) throw std::runtime_error("결과 파일 저장 실패");
                };
                std::ostringstream config;
                write_named_experiment(config, input);
                save("yuki_test.ini", config.str()); save("yuki_test.txt", report.str()); save("yuki_test.json", json.str());
                out << "outputs/yuki_test.ini, .txt, .json 저장 완료 (같은 이름의 이전 결과를 갱신합니다).\n";
            } else throw std::invalid_argument("메뉴 번호를 입력하세요.");
            input = std::move(changed);
        } catch (const std::exception& e) {
            out << "오류: " << e.what() << '\n';
            if (!in) return 0;
        }
    }
}
} // namespace er
