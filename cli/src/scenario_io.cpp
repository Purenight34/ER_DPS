#include "er/scenario_io.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace er {
namespace {

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t");
    return std::string(text.substr(first, last - first + 1));
}

void validate_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            if ((lead < 0x20 && lead != '\t') || lead == 0x7f)
                throw std::invalid_argument("unexpected control character");
            ++i;
            continue;
        }
        std::size_t count;
        unsigned int codepoint;
        unsigned int minimum;
        if (lead >= 0xc2 && lead <= 0xdf) {
            count = 2; codepoint = lead & 0x1f; minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            count = 3; codepoint = lead & 0x0f; minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            count = 4; codepoint = lead & 0x07; minimum = 0x10000;
        } else {
            throw std::invalid_argument("invalid UTF-8");
        }
        if (count > text.size() - i) throw std::invalid_argument("truncated UTF-8");
        for (std::size_t j = 1; j < count; ++j) {
            const auto next = static_cast<unsigned char>(text[i + j]);
            if ((next & 0xc0) != 0x80) throw std::invalid_argument("invalid UTF-8 continuation");
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
            throw std::invalid_argument("invalid UTF-8 codepoint");
        i += count;
    }
}

template <typename T>
T number(const std::string& value) {
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument("invalid or out-of-range number: " + value);
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(result)) throw std::invalid_argument("number must be finite");
    }
    return result;
}

std::vector<std::string> fields(const std::string& value, std::size_t count) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto next = value.find(',', start);
        result.push_back(trim(std::string_view(value).substr(start, next == std::string::npos ? next : next - start)));
        if (result.back().empty()) throw std::invalid_argument("empty comma-separated field");
        if (next == std::string::npos) break;
        start = next + 1;
    }
    if (result.size() != count) throw std::invalid_argument("expected " + std::to_string(count) + " comma-separated fields");
    return result;
}

std::string quoted(const std::string& value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (ch < 0x20) {
                result += "\\u00";
                result += hex[ch >> 4];
                result += hex[ch & 0x0f];
            } else {
                result += static_cast<char>(ch);
            }
        }
    }
    return result + '"';
}

void prepare_output(std::ostream& output) {
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << std::defaultfloat << std::dec << std::noshowpos << std::noshowpoint;
}

void check_output(std::ostream& output) {
    if (!output) throw std::runtime_error("failed to write result");
}

void text_stat_input(std::ostream& out, const std::string& prefix, const StatInput& stat) {
    out << prefix << ".base=" << stat.base << '\n'
        << prefix << ".per_level=" << stat.per_level << '\n';
    for (const auto& contribution : stat.flat)
        out << prefix << ".flat=" << contribution.source_id << ',' << contribution.value << '\n';
}

void text_combatant(std::ostream& out, const std::string& prefix, const Combatant& actor) {
    out << prefix << ".character_id=" << actor.character_id << '\n'
        << prefix << ".weapon_id=" << actor.weapon_id << '\n'
        << prefix << ".equipment=" << actor.equipment << '\n'
        << prefix << ".traits=" << actor.traits << '\n'
        << prefix << ".masteries=" << actor.masteries << '\n'
        << prefix << ".skill_levels=" << actor.skill_levels << '\n'
        << prefix << ".initial_effects=" << actor.initial_effects << '\n'
        << prefix << ".level=" << actor.level << '\n';
    text_stat_input(out, prefix + ".attack_power", actor.attack_power);
    text_stat_input(out, prefix + ".defense", actor.defense);
    text_stat_input(out, prefix + ".max_hp", actor.max_hp);
}

void text_stat_result(std::ostream& out, const std::string& name, const StatResult& stat) {
    out << name << ": base=" << stat.base << ", growth=" << stat.growth
        << ", flat=" << stat.flat << ", total=" << stat.total << '\n';
}

void json_stat_input(std::ostream& out, const StatInput& stat) {
    out << "{\"base\":" << stat.base << ",\"per_level\":" << stat.per_level << ",\"flat\":[";
    bool first = true;
    for (const auto& contribution : stat.flat) {
        if (!first) out << ',';
        first = false;
        out << "{\"source_id\":" << quoted(contribution.source_id) << ",\"value\":" << contribution.value << '}';
    }
    out << "]}";
}

void json_combatant(std::ostream& out, const Combatant& actor) {
    out << "{\"character_id\":" << quoted(actor.character_id)
        << ",\"weapon_id\":" << quoted(actor.weapon_id)
        << ",\"equipment\":" << quoted(actor.equipment)
        << ",\"traits\":" << quoted(actor.traits)
        << ",\"masteries\":" << quoted(actor.masteries)
        << ",\"skill_levels\":" << quoted(actor.skill_levels)
        << ",\"initial_effects\":" << quoted(actor.initial_effects)
        << ",\"level\":" << actor.level << ",\"attack_power\":";
    json_stat_input(out, actor.attack_power);
    out << ",\"defense\":";
    json_stat_input(out, actor.defense);
    out << ",\"max_hp\":";
    json_stat_input(out, actor.max_hp);
    out << '}';
}

void json_stat_result(std::ostream& out, const StatResult& stat) {
    out << "{\"base\":" << stat.base << ",\"growth\":" << stat.growth
        << ",\"flat\":" << stat.flat << ",\"total\":" << stat.total << '}';
}

void json_stats(std::ostream& out, const PermanentStats& stats) {
    out << "{\"attack_power\":";
    json_stat_result(out, stats.attack_power);
    out << ",\"defense\":";
    json_stat_result(out, stats.defense);
    out << ",\"max_hp\":";
    json_stat_result(out, stats.max_hp);
    out << '}';
}

void json_comparison(std::ostream& out, const std::optional<Comparison>& comparison) {
    if (!comparison) {
        out << "null";
        return;
    }
    out << "{\"observed\":" << comparison->observed << ",\"delta\":" << comparison->delta << ",\"percent\":";
    if (comparison->percent) out << *comparison->percent;
    else out << "null";
    out << '}';
}

void text_comparison(std::ostream& out, const std::optional<Comparison>& comparison) {
    if (!comparison) {
        out << "unmeasured";
        return;
    }
    out << "observed=" << comparison->observed << ", calculated_minus_observed=" << comparison->delta << ", error_percent=";
    if (comparison->percent) out << *comparison->percent;
    else out << "undefined(zero_observed_or_unrepresentable_ratio)";
}

} // namespace

Scenario read_scenario(std::istream& input) {
    Scenario scenario;
    std::map<std::string, std::function<void(const std::string&)>> setters;
    std::map<std::string, StatInput*> flat_fields;
    auto string_field = [&](const std::string& key, std::string& destination) {
        setters.emplace(key, [&destination](const std::string& value) {
            if (value.starts_with("PLACEHOLDER"))
                throw std::invalid_argument("replace PLACEHOLDER with recorded experiment metadata");
            destination = value;
        });
    };
    auto numeric_field = [&]<typename T>(const std::string& key, T& destination) {
        setters.emplace(key, [&destination](const std::string& value) { destination = number<T>(value); });
    };
    string_field("id", scenario.id);
    string_field("patch_version", scenario.patch_version);
    string_field("data_version", scenario.data_version);
    string_field("data_source", scenario.data_source);
    string_field("formula_version", scenario.formula_version);
    string_field("game_mode", scenario.game_mode);
    string_field("notes", scenario.notes);
    auto add_stat = [&](const std::string& prefix, StatInput& stat) {
        numeric_field(prefix + ".base", stat.base);
        numeric_field(prefix + ".per_level", stat.per_level);
        flat_fields.emplace(prefix + ".flat", &stat);
    };
    auto add_actor = [&](const std::string& prefix, Combatant& actor) {
        string_field(prefix + ".character_id", actor.character_id);
        string_field(prefix + ".weapon_id", actor.weapon_id);
        string_field(prefix + ".equipment", actor.equipment);
        string_field(prefix + ".traits", actor.traits);
        string_field(prefix + ".masteries", actor.masteries);
        string_field(prefix + ".skill_levels", actor.skill_levels);
        string_field(prefix + ".initial_effects", actor.initial_effects);
        numeric_field(prefix + ".level", actor.level);
        add_stat(prefix + ".attack_power", actor.attack_power);
        add_stat(prefix + ".defense", actor.defense);
        add_stat(prefix + ".max_hp", actor.max_hp);
    };
    add_actor("attacker", scenario.attacker);
    add_actor("defender", scenario.defender);
    numeric_field("attack_speed", scenario.attack_speed);
    numeric_field("critical_chance", scenario.critical_chance);
    numeric_field("critical_multiplier", scenario.critical_multiplier);
    string_field("amplification_level_source", scenario.amplification_level_source);
    numeric_field("amplification_levels", scenario.amplification_levels);
    numeric_field("amplification_per_level", scenario.amplification_per_level);
    numeric_field("initial_hp", scenario.initial_hp);
    numeric_field("initial_shield", scenario.initial_shield);
    numeric_field("duration_ms", scenario.duration_ms);
    setters.emplace("unsupported_effects", [&](const std::string& value) {
        if (value != "true" && value != "false") throw std::invalid_argument("expected true or false");
        scenario.unsupported_effects = value == "true";
    });

    std::set<std::string> seen;
    std::set<std::string> attack_ids;
    std::size_t line_number = 0;
    std::string line;
    while (std::getline(input, line)) {
        ++line_number;
        if (line_number == 1 && line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        try {
            validate_utf8(line);
            line = trim(line);
            if (line.empty() || line.front() == '#') continue;
            const auto separator = line.find('=');
            if (separator == std::string::npos) throw std::invalid_argument("expected key=value");
            const auto key = trim(std::string_view(line).substr(0, separator));
            const auto value = trim(std::string_view(line).substr(separator + 1));
            if (key.empty() || value.empty()) throw std::invalid_argument("key and value must be nonempty");
            if (key == "attack") {
                const auto parts = fields(value, 5);
                if (!attack_ids.insert(parts[0]).second) throw std::invalid_argument("duplicate attack ID: " + parts[0]);
                if (parts[2] != "hit" && parts[2] != "miss") throw std::invalid_argument("expected hit or miss");
                if (parts[3] != "normal" && parts[3] != "critical") throw std::invalid_argument("expected normal or critical");
                Attack attack{parts[0], number<std::int64_t>(parts[1]), parts[2] == "hit", parts[3] == "critical", std::nullopt};
                if (parts[4] != "na") attack.observed_damage = number<double>(parts[4]);
                scenario.attacks.push_back(std::move(attack));
            } else if (const auto flat = flat_fields.find(key); flat != flat_fields.end()) {
                const auto parts = fields(value, 2);
                auto& contributions = flat->second->flat;
                if (std::any_of(contributions.begin(), contributions.end(), [&](const auto& item) { return item.source_id == parts[0]; }))
                    throw std::invalid_argument("duplicate stat contribution source: " + parts[0]);
                contributions.push_back({parts[0], number<double>(parts[1])});
            } else if (const auto field = setters.find(key); field != setters.end()) {
                if (!seen.insert(key).second) throw std::invalid_argument("duplicate key: " + key);
                field->second(value);
            } else {
                throw std::invalid_argument("unknown key: " + key);
            }
        } catch (const std::exception& error) {
            throw std::invalid_argument("line " + std::to_string(line_number) + ": " + error.what());
        }
    }
    if (!input.eof() || input.bad()) throw std::runtime_error("failed to read scenario input");
    for (const auto& [key, unused] : setters) {
        static_cast<void>(unused);
        if (!seen.contains(key)) throw std::invalid_argument("missing required key: " + key);
    }
    if (scenario.attacks.empty()) throw std::invalid_argument("at least one attack entry is required");
    return scenario;
}

void write_text(std::ostream& out, const Scenario& scenario, const Result& result) {
    prepare_output(out);
    out << "Basic attack experiment (temporary, user-approved model; not verified game truth)\n"
        << "No game rounding or critical RNG is applied. Full internal precision follows.\n"
        << "Formula: stat = base + per_level * (level - 1) + sum(flat)\n"
        << "Formula: critical_factor = critical_multiplier for critical, 1 for normal\n"
        << "Formula: raw = attack_power * (1 + amplification_levels * amplification_per_level) * critical_factor\n"
        << "Formula: hit_damage = raw * 100 / (100 + defense); miss_damage = 0\n"
        << "Formula: combo_dps = total_damage / (duration_ms / 1000)\n\n"
        << "[Reproduction inputs]\n"
        << "id=" << scenario.id << '\n'
        << "patch_version=" << scenario.patch_version << '\n'
        << "data_version=" << scenario.data_version << '\n'
        << "data_source=" << scenario.data_source << '\n'
        << "formula_version=" << scenario.formula_version << '\n'
        << "game_mode=" << scenario.game_mode << '\n'
        << "notes=" << scenario.notes << '\n';
    text_combatant(out, "attacker", scenario.attacker);
    text_combatant(out, "defender", scenario.defender);
    out << "attack_speed=" << scenario.attack_speed << '\n'
        << "critical_chance=" << scenario.critical_chance << '\n'
        << "critical_multiplier=" << scenario.critical_multiplier << '\n'
        << "amplification_level_source=" << scenario.amplification_level_source << '\n'
        << "amplification_levels=" << scenario.amplification_levels << '\n'
        << "amplification_per_level=" << scenario.amplification_per_level << '\n'
        << "initial_hp=" << scenario.initial_hp << '\n'
        << "initial_shield=" << scenario.initial_shield << '\n'
        << "duration_ms=" << scenario.duration_ms << '\n'
        << "unsupported_effects=" << (scenario.unsupported_effects ? "true" : "false") << '\n';
    for (const auto& attack : scenario.attacks) {
        out << "attack=" << attack.id << ',' << attack.time_ms << ',' << (attack.hit ? "hit" : "miss")
            << ',' << (attack.critical ? "critical" : "normal") << ',';
        if (attack.observed_damage) out << *attack.observed_damage;
        else out << "na";
        out << '\n';
    }
    out << "\n[Permanent stat contributions]\n";
    text_stat_result(out, "attacker.attack_power", result.attacker.attack_power);
    text_stat_result(out, "attacker.defense", result.attacker.defense);
    text_stat_result(out, "attacker.max_hp", result.attacker.max_hp);
    text_stat_result(out, "defender.attack_power", result.defender.attack_power);
    text_stat_result(out, "defender.defense", result.defender.defense);
    text_stat_result(out, "defender.max_hp", result.defender.max_hp);
    out << "basic_attack_amplification=" << result.basic_attack_amplification << " (fraction)\n"
        << "\n[Attack results: remaining_hp includes overkill; current_hp is clamped at zero]\n";
    for (const auto& impact : result.impacts) {
        out << "id=" << impact.id << ", time_ms=" << impact.time_ms << ", sequence_index=" << impact.sequence_index
            << ", " << (impact.hit ? "hit" : "miss") << ", " << (impact.critical ? "critical" : "normal")
            << ", raw_damage=" << impact.raw_damage << ", defense_multiplier=" << impact.defense_multiplier
            << ", total_damage=" << impact.total_damage << ", shield_damage=" << impact.shield_damage
            << ", health_damage=" << impact.health_damage << ", remaining_shield=" << impact.remaining_shield
            << ", remaining_hp=" << impact.remaining_hp << ", current_hp=" << impact.current_hp << ", ";
        text_comparison(out, impact.comparison);
        out << '\n';
    }
    out << "\n[Combo totals]\n"
        << "total_damage=" << result.total_damage << '\n'
        << "shield_damage=" << result.shield_damage << '\n'
        << "health_damage=" << result.health_damage << '\n'
        << "remaining_shield=" << result.remaining_shield << '\n'
        << "remaining_hp=" << result.remaining_hp << '\n'
        << "current_hp=" << result.current_hp << '\n'
        << "duration_ms=" << scenario.duration_ms << '\n'
        << "combo_dps=" << result.combo_dps << '\n'
        << "\n[Observed subset only; unmeasured impacts are excluded from this comparison]\n"
        << "measured_count=" << result.measured_count << '\n'
        << "measured_calculated_damage=" << result.measured_calculated_damage << '\n';
    text_comparison(out, result.measured_comparison);
    out << "\n\n[Warnings]\n";
    for (const auto& warning : result.warnings) out << "- " << warning << '\n';
    check_output(out);
}

void write_json(std::ostream& out, const Scenario& scenario, const Result& result) {
    prepare_output(out);
    out << "{\"schema_version\":\"er-basic-attack-report-v1\","
        << "\"rule_status\":\"user-approved temporary model; not verified game truth\","
        << "\"scenario\":{\"id\":" << quoted(scenario.id)
        << ",\"patch_version\":" << quoted(scenario.patch_version)
        << ",\"data_version\":" << quoted(scenario.data_version)
        << ",\"data_source\":" << quoted(scenario.data_source)
        << ",\"formula_version\":" << quoted(scenario.formula_version)
        << ",\"game_mode\":" << quoted(scenario.game_mode)
        << ",\"notes\":" << quoted(scenario.notes) << ",\"attacker\":";
    json_combatant(out, scenario.attacker);
    out << ",\"defender\":";
    json_combatant(out, scenario.defender);
    out << ",\"attack_speed\":" << scenario.attack_speed
        << ",\"critical_chance\":" << scenario.critical_chance
        << ",\"critical_multiplier\":" << scenario.critical_multiplier
        << ",\"amplification_level_source\":" << quoted(scenario.amplification_level_source)
        << ",\"amplification_levels\":" << scenario.amplification_levels
        << ",\"amplification_per_level\":" << scenario.amplification_per_level
        << ",\"initial_hp\":" << scenario.initial_hp
        << ",\"initial_shield\":" << scenario.initial_shield
        << ",\"duration_ms\":" << scenario.duration_ms
        << ",\"unsupported_effects\":" << (scenario.unsupported_effects ? "true" : "false")
        << ",\"attacks\":[";
    bool first = true;
    for (const auto& attack : scenario.attacks) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << quoted(attack.id) << ",\"time_ms\":" << attack.time_ms
            << ",\"hit\":" << (attack.hit ? "true" : "false")
            << ",\"critical\":" << (attack.critical ? "true" : "false") << ",\"observed_damage\":";
        if (attack.observed_damage) out << *attack.observed_damage;
        else out << "null";
        out << '}';
    }
    out << "]},\"result\":{\"attacker\":";
    json_stats(out, result.attacker);
    out << ",\"defender\":";
    json_stats(out, result.defender);
    out << ",\"basic_attack_amplification\":" << result.basic_attack_amplification << ",\"impacts\":[";
    first = true;
    for (const auto& impact : result.impacts) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << quoted(impact.id) << ",\"time_ms\":" << impact.time_ms
            << ",\"sequence_index\":" << impact.sequence_index
            << ",\"hit\":" << (impact.hit ? "true" : "false")
            << ",\"critical\":" << (impact.critical ? "true" : "false")
            << ",\"raw_damage\":" << impact.raw_damage
            << ",\"defense_multiplier\":" << impact.defense_multiplier
            << ",\"total_damage\":" << impact.total_damage
            << ",\"shield_damage\":" << impact.shield_damage
            << ",\"health_damage\":" << impact.health_damage
            << ",\"remaining_shield\":" << impact.remaining_shield
            << ",\"remaining_hp\":" << impact.remaining_hp
            << ",\"current_hp\":" << impact.current_hp << ",\"comparison\":";
        json_comparison(out, impact.comparison);
        out << '}';
    }
    out << "],\"total_damage\":" << result.total_damage
        << ",\"shield_damage\":" << result.shield_damage
        << ",\"health_damage\":" << result.health_damage
        << ",\"remaining_hp\":" << result.remaining_hp
        << ",\"remaining_shield\":" << result.remaining_shield
        << ",\"current_hp\":" << result.current_hp
        << ",\"combo_dps\":" << result.combo_dps
        << ",\"measured_count\":" << result.measured_count
        << ",\"measured_calculated_damage\":" << result.measured_calculated_damage
        << ",\"measured_comparison\":";
    json_comparison(out, result.measured_comparison);
    out << ",\"warnings\":[";
    first = true;
    for (const auto& warning : result.warnings) {
        if (!first) out << ',';
        first = false;
        out << quoted(warning);
    }
    out << "]}}\n";
    check_output(out);
}

} // namespace er
