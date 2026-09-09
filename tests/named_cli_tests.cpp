#include "er/named_cli.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool ok) { ++checks; if (!ok) throw std::runtime_error("named CLI check failed"); }
void rejects(const std::string& text) {
    ++checks;
    std::istringstream in(text);
    try { (void)er::read_named_experiment(in); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("invalid named input accepted");
}
}
int main() {
    try {
        std::istringstream minimal("[attacker]\ncharacter=유키\nweapon=장검\nlevel=6\nweapon_mastery=7\narmor=천 갑옷\ntrait=취약\n[experiment]\nattack=hit1,0,hit,normal,33\n");
        auto input = er::read_named_experiment(minimal);
        check(input.attacker.character == "유키");
        check(input.attacker.weapon == "장검" && input.attacker.level == 6 && input.attacker.weapon_mastery == 7);
        check(input.attacker.armor.size() == 1 && input.attacker.traits.at(0) == "취약");
        check(input.attacks.size() == 1 && input.attacks[0].observed_damage == 33);
        check(!input.final_attack_speed && !input.final_critical_multiplier);
        check(input.defender_kind == er::DefenderKind::Dummy && input.dummy_max_hp == 1000 && input.dummy_defense == 0);
        input.dummy_max_hp = 1750.25;
        input.dummy_defense = 40.5;
        input.final_attack_speed = 1.27;
        input.final_critical_chance = 0.125;
        input.final_critical_multiplier = 1.75;
        input.defender_initial_hp = 815;
        input.defender_initial_shield = 20;
        input.duration_ms = 2400;
        input.attacks.push_back({"a2", 1000, false, true, 0});
        std::ostringstream saved;
        er::write_named_experiment(saved, input);
        std::istringstream restored_stream(saved.str());
        const auto restored = er::read_named_experiment(restored_stream);
        check(restored.attacker.weapon == input.attacker.weapon);
        check(restored.attacker.traits == input.attacker.traits);
        check(restored.final_attack_speed == input.final_attack_speed);
        check(restored.final_critical_multiplier == input.final_critical_multiplier);
        check(restored.defender_initial_hp == 815 && restored.defender_initial_shield == 20);
        check(restored.attacks.size() == 2 && !restored.attacks[1].hit && restored.attacks[1].critical);
        check(restored.duration_ms == 2400);
        check(restored.defender_kind == er::DefenderKind::Dummy && restored.dummy_max_hp == 1750.25 && restored.dummy_defense == 40.5);
        check(saved.str().find("[defender]\ntype=dummy\nmax_hp=1750.25\ndefense=40.5\n") != std::string::npos);
        std::istringstream legacy("[defender]\ncharacter=유키\nweapon=장검\nweapon_mastery=7\n");
        const auto old_input = er::read_named_experiment(legacy);
        check(old_input.defender_kind == er::DefenderKind::Character && old_input.defender.weapon == "장검");
        std::ostringstream old_saved;
        er::write_named_experiment(old_saved, old_input);
        std::istringstream old_restored(old_saved.str());
        check(er::read_named_experiment(old_restored).defender_kind == er::DefenderKind::Character);
        std::istringstream implicit_dummy("[defender]\nmax_hp=1200\ndefense=12\n");
        check(er::read_named_experiment(implicit_dummy).defender_kind == er::DefenderKind::Dummy);
        rejects("[defender]\ntype=typo\n");
        rejects("[defender]\ntype=dummy\nweapon_mastery=5\n");
        rejects("[defender]\ncharacter=유키\ntype=dummy\n");
        rejects("[defender]\ntype=character\nmax_hp=1000\n");
        rejects("[defender]\ndefense=5\ncharacter=유키\n");
        rejects("[defender]\ntype=dummy\ntype=character\n");
        rejects("[attacker]\nlevle=3\n");
        rejects("[attacker]\nlevel=1\nlevel=2\n");
        rejects("[attacker]\nlevel=3junk\n");
        rejects("[experiment]\nfinal_attack_speed=nan\n");
        rejects("[experiment]\nfinal_attack_speed=1,2\n");
        rejects("[experiment]\nattack=a,0,maybe,normal,na\n");
        rejects("[experiment]\nattack=a,0,hit,crit,na\n");
        rejects("[experiment]\nattack=a,0,hit,normal\n");
        rejects("[defender]\ncharacter=\n");
        rejects("[unknown]\nlevel=1\n");
        rejects("[attacker]\ncharacter=\xff\n");
        std::istringstream bom("\xef\xbb\xbf[attacker]\r\ncharacter=유키\r\n");
        check(er::read_named_experiment(bom).attacker.character == "유키");
        std::cout << "Passed " << checks << " named CLI checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
