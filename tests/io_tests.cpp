#include "er/scenario_io.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}

std::string fixture() {
    std::string text =
        "# Synthetic input; no game data.\n"
        "id=io-fixture\npatch_version=synthetic\ndata_version=fixture-v1\n"
        "data_source=synthetic fixture\nformula_version=basic-attack-experiment-v1\n"
        "game_mode=synthetic\nnotes=quoted \"text\" and \\ path # retained\n";
    for (const std::string actor : {"attacker", "defender"}) {
        text += actor + ".character_id=synthetic-character\n";
        text += actor + ".weapon_id=synthetic-weapon\n";
        for (const std::string field : {"equipment", "traits", "masteries", "skill_levels", "initial_effects"}) {
            text += actor + "." + field + "=none\n";
        }
        text += actor + ".level=6\n";
        for (const std::string stat : {"attack_power", "defense", "max_hp"}) {
            text += actor + "." + stat + ".base=100\n";
            text += actor + "." + stat + ".per_level=0\n";
        }
    }
    text += "attacker.attack_power.flat=synthetic-item,10.25\n"
        "attack_speed=1\ncritical_chance=0.5\ncritical_multiplier=1.75\n"
        "amplification_level_source=weapon_mastery\namplification_levels=10\n"
        "amplification_per_level=0.02\ninitial_hp=60\ninitial_shield=10\n"
        "duration_ms=4000\nunsupported_effects=false\n"
        "attack=a1,0,hit,normal,60\nattack=a2,1000,hit,critical,105\n"
        "attack=a3,2000,miss,normal,na\nattack=a4,3000,hit,normal,60\n";
    return text;
}

er::Scenario parse(const std::string& input) {
    std::istringstream stream(input);
    return er::read_scenario(stream);
}

void rejects(const std::string& input, const std::string& label) {
    try {
        static_cast<void>(parse(input));
        check(false, label);
    } catch (const std::exception&) {
    }
}

std::string replace_once(std::string input, const std::string& before, const std::string& after) {
    const auto pos = input.find(before);
    if (pos == std::string::npos) throw std::logic_error("invalid test replacement");
    input.replace(pos, before.size(), after);
    return input;
}
} // namespace

int main() {
    try {
        const auto valid = fixture();
        const auto scenario = parse(valid);
        check(scenario.attacks.size() == 4, "repeated attacks retained");
        check(scenario.attacks[1].critical && scenario.attacks[1].observed_damage == 105.0,
              "explicit critical outcome and observation");
        check(!scenario.attacks[2].hit && !scenario.attacks[2].observed_damage,
              "miss and unmeasured observation");
        check(scenario.attacker.attack_power.flat[0].value == 10.25, "fractional source contribution");
        check(scenario.amplification_levels == 10 && scenario.amplification_level_source == "weapon_mastery",
              "explicit weapon mastery basis");
        check(scenario.notes.find("# retained") != std::string::npos, "inline hash preserved as data");

        std::string crlf = "\xEF\xBB\xBF";
        for (char ch : valid) crlf += ch == '\n' ? "\r\n" : std::string(1, ch);
        check(parse(crlf).id == scenario.id, "UTF-8 BOM and CRLF");
        check(parse(replace_once(valid, "notes=quoted", "notes=한글 quoted")).notes.starts_with("한글"),
              "UTF-8 metadata");

        rejects(valid + "unknown_key=1\n", "unknown key");
        rejects(valid + "attack_speed=1\n", "duplicate scalar");
        rejects(replace_once(valid, "initial_shield=10\n", ""), "missing explicit field");
        rejects(replace_once(valid, "notes=quoted \"text\" and \\ path # retained", "notes="), "empty metadata");
        rejects(replace_once(valid, "data_source=synthetic fixture", "data_source=PLACEHOLDER-source"), "unfilled source placeholder");
        rejects(replace_once(valid, "attacker.character_id=synthetic-character", "attacker.character_id=PLACEHOLDER"), "unfilled actor placeholder");
        rejects(replace_once(valid, "attack_speed=1", "attack_speed=1oops"), "trailing numeric junk");
        rejects(replace_once(valid, "attack_speed=1", "attack_speed=nan"), "non-finite number");
        rejects(replace_once(valid, "attack_speed=1", "attack_speed=1e9999"), "numeric overflow");
        rejects(replace_once(valid, "duration_ms=4000", "duration_ms=4.0"), "integer time required");
        rejects(replace_once(valid, "unsupported_effects=false", "unsupported_effects=0"), "strict boolean");
        rejects(replace_once(valid, "attack=a1,0,hit,normal,60", "attack=a1,0,hit,normal,60,extra"), "extra attack field");
        rejects(replace_once(valid, "attack=a1,0,hit,normal,60", "attack=a1,0,maybe,normal,60"), "unknown hit state");
        rejects(replace_once(valid, "attack=a1,0,hit,normal,60", "attack=a1,0,hit,random,60"), "unknown critical state");
        rejects(replace_once(valid, "attack=a2,", "attack=a1,"), "duplicate event ID");
        rejects(valid + "attacker.attack_power.flat=synthetic-item,2\n", "duplicate contribution source");
        rejects(replace_once(valid, "synthetic-item,10.25", "synthetic-item,10.25,2"), "extra contribution field");
        rejects(replace_once(valid, "data_source=synthetic fixture", "data_source=bad\xC0\xAF"), "invalid UTF-8");
        rejects(replace_once(valid, "id=io-fixture", std::string("id=bad\0value", 12)), "embedded NUL");
        rejects("# only comments\n", "missing all fields");
        rejects(replace_once(valid, "attack_speed=1", "attack_speed 1"), "missing key separator");
        {
            std::istringstream stream(valid);
            stream.setstate(std::ios::badbit);
            try {
                static_cast<void>(er::read_scenario(stream));
                check(false, "stream read failure");
            } catch (const std::exception&) {
            }
        }

        er::Result result;
        result.total_damage = 225;
        result.health_damage = 215;
        result.shield_damage = 10;
        result.remaining_hp = -155;
        result.combo_dps = 56.25;
        result.basic_attack_amplification = 0.2;
        result.measured_count = 3;
        result.measured_calculated_damage = 225;
        result.measured_comparison = er::Comparison{225, 0, 0};
        result.warnings.push_back("synthetic \"warning\"\nsecond line");
        result.impacts.push_back(er::ImpactResult{"a1", 0, 0, true, false, 120, 0.5, 60, 10, 50, 0, 10, 10,
                                                 er::Comparison{60, 0, 0}});
        std::ostringstream json;
        er::write_json(json, scenario, result);
        const auto serialized = json.str();
        check(serialized.find("\"scenario\":") != std::string::npos, "JSON includes raw scenario");
        check(serialized.find("\"formula_version\":\"basic-attack-experiment-v1\"") != std::string::npos,
              "JSON includes formula version");
        check(serialized.find("\\\"text\\\" and \\\\ path # retained") != std::string::npos, "JSON escaping");
        check(serialized.find("\\nsecond line") != std::string::npos, "JSON control escaping");
        check(serialized.find("\"observed_damage\":null") != std::string::npos, "unmeasured stays null");
        check(serialized.find("\"measured_count\":3") != std::string::npos, "partial comparison count");
        check(serialized.find("\"remaining_hp\":-155") != std::string::npos, "negative bookkeeping HP retained");
        check(serialized.find("\"source_id\":\"synthetic-item\"") != std::string::npos, "individual stat sources retained");
        std::ostringstream text;
        er::write_text(text, scenario, result);
        check(text.str().find("temporary") != std::string::npos, "temporary rule status in text");
        check(text.str().find("amplification_level_source=weapon_mastery") != std::string::npos, "text reproduces input");
        check(text.str().find("measured_count=3") != std::string::npos, "text reports measured subset");
        check(text.str().find("defense_multiplier") != std::string::npos, "text reports mitigation trace");
        for (bool as_json : {false, true}) {
            std::ostringstream failed;
            failed.setstate(std::ios::badbit);
            try {
                if (as_json) er::write_json(failed, scenario, result);
                else er::write_text(failed, scenario, result);
                check(false, "output stream failure");
            } catch (const std::exception&) {
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Unexpected test error: " << error.what() << '\n';
        return 1;
    }
    if (failures) return 1;
    std::cout << "All scenario IO tests passed.\n";
    return 0;
}
