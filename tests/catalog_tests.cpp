#include "er/catalog.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;
int checks = 0;

void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}

template <typename Action>
void rejects(Action action, const std::string& label, const std::string& message = "") {
    try {
        action();
        check(false, label);
    } catch (const std::exception& error) {
        check(message.empty() || std::string(error.what()).find(message) != std::string::npos,
              label + " (error context)");
    }
}

std::string fixture() {
    // Deliberately synthetic statistics and IDs, unrelated to game balance.
    return "# er-catalog-v1\n"
        "Meta\t0\tdata_version\tfixture-v1\n"
        "Meta\t0\tpatch_version\tsynthetic\n"
        "Meta\t0\tsource\tsynthetic offline fixture\n"
        "Character\t4\tcode\t1001\n"
        "Character\t4\t_name\t유키\n"
        "Character\t4\tattackPower\t12.25\n"
        "ItemWeapon\t0\tcode\t3001\n"
        "ItemWeapon\t0\t_name\t테스트 검\n"
        "Character\t4\t_description\t첫 줄\\n둘째\\t칸\\r끝\\\\경로\n"
        "Character\t10\tcode\t1002\n"
        "Character\t10\t_name\t테스트 실험체\n"
        "FutureTable\t7\tunknownField\t{\"future\":true}\n"
        "ItemWeapon\t0\temptyField\t\n";
}

er::Catalog parse(const std::string& text) {
    std::istringstream input(text);
    return er::Catalog::read(input);
}

std::string replace_once(std::string text, const std::string& old, const std::string& replacement) {
    const auto pos = text.find(old);
    if (pos == std::string::npos) throw std::logic_error("test replacement missing");
    text.replace(pos, old.size(), replacement);
    return text;
}
} // namespace

int main() {
    try {
        const auto text = fixture();
        const auto catalog = parse(text);
        check(catalog.records().size() == 5, "rows aggregate interleaved fields");
        check(catalog.records()[2].table == "ItemWeapon", "record order follows first appearance");
        check(catalog.table("Character").size() == 2, "filter by table");
        check(catalog.table("Missing").empty(), "missing table is empty");
        check(catalog.metadata().require("data_version") == "fixture-v1", "version provenance retained");
        check(catalog.metadata().require("patch_version") == "synthetic", "patch provenance retained");
        check(catalog.metadata().require("source") == "synthetic offline fixture", "source retained");
        const auto& yuki = catalog.find("Character", "유키");
        check(yuki.require("code") == "1001", "UTF-8 name lookup");
        check(&catalog.find("Character", "1001") == &yuki, "official code lookup");
        check(yuki.row_id == "4", "source row ID distinct from official code");
        check(yuki.number("attackPower") == 12.25, "fractional field conversion");
        check(yuki.require("_description") == "첫 줄\n둘째\t칸\r끝\\경로", "strict escape round trip");
        check(yuki.value("absent", "fallback") == "fallback", "explicit optional fallback");
        check(yuki.value("absent").empty(), "default optional fallback");
        check(catalog.table("FutureTable")[0]->require("unknownField") == "{\"future\":true}",
              "unknown tables and fields preserved");
        check(catalog.find("ItemWeapon", "3001").require("emptyField").empty(), "empty value preserved");
        check(catalog.search("Character", "실험").size() == 1, "substring name search");
        check(catalog.search("Character", "100").size() == 2, "substring code search");
        check(catalog.search("Character", "").size() == 2, "empty query lists table");
        check(catalog.search("Character", "없는 이름").empty(), "search without matches");
        check(catalog.search("Missing", "").empty(), "search missing table");
        rejects([&] { catalog.find("Character", "유"); }, "find never silently uses prefix", "유");
        rejects([&] { catalog.find("Character", "4"); }, "row ID cannot replace official code");
        rejects([&] { catalog.find("Character", ""); }, "empty exact lookup rejected");
        rejects([&] { yuki.require("absent"); }, "missing required field", "absent");
        rejects([&] { yuki.number("absent"); }, "missing number", "absent");

        const auto ambiguous = parse(text + "Character\t11\tcode\t1003\nCharacter\t11\t_name\t유키\n");
        rejects([&] { ambiguous.find("Character", "유키"); }, "duplicate names list first code", "1001");
        rejects([&] { ambiguous.find("Character", "유키"); }, "duplicate names list other code", "1003");
        check(ambiguous.find("Character", "1003").row_id == "11", "code disambiguates duplicate names");
        const auto duplicate_code = parse(text + "Character\t11\tcode\t1001\n");
        rejects([&] { duplicate_code.find("Character", "1001"); }, "duplicate codes rejected at lookup");

        std::string crlf = "\xEF\xBB\xBF";
        for (const char c : text) crlf += c == '\n' ? "\r\n" : std::string(1, c);
        check(parse(crlf).find("Character", "유키").number("attackPower") == 12.25, "BOM and CRLF input");
        check(parse(text.substr(0, text.size() - 1)).records().size() == 5, "final newline optional");
        for (const auto* invalid : {"", "nan", "inf", "-inf", "1e9999", "1.2oops", " 1", "1 ", "+1"}) {
            const auto numbers = parse(replace_once(text, "attackPower\t12.25", std::string("attackPower\t") + invalid));
            rejects([&] { numbers.find("Character", "1001").number("attackPower"); },
                    std::string("invalid numeric token: ") + invalid, "attackPower");
        }
        check(parse(replace_once(text, "attackPower\t12.25", "attackPower\t-1.25e2"))
                  .find("Character", "1001").number("attackPower") == -125.0,
              "signed finite exponent token");

        rejects([&] { parse(""); }, "empty file");
        rejects([&] { parse(replace_once(text, "# er-catalog-v1", "# er-catalog-v2")); }, "unsupported schema");
        rejects([&] { parse(replace_once(text, "# er-catalog-v1\n", "")); }, "header mandatory");
        rejects([&] { parse(replace_once(text, "Meta\t0\tdata_version\tfixture-v1\n", "")); }, "data version mandatory");
        rejects([&] { parse(replace_once(text, "Meta\t0\tpatch_version\tsynthetic\n", "")); }, "patch version mandatory");
        rejects([&] { parse(replace_once(text, "Meta\t0\tsource\tsynthetic offline fixture\n", "")); }, "source mandatory");
        rejects([&] { parse(replace_once(text, "data_version\tfixture-v1", "data_version\t")); }, "empty version rejected");
        rejects([&] { parse(text + "Character\t4\tattackPower\t12.25\n"); }, "duplicate field even if identical", "attackPower");
        rejects([&] { parse(text + "Character\t0\tfield\n"); }, "three columns rejected");
        rejects([&] { parse(text + "Character\t0\tfield\tvalue\textra\n"); }, "five columns rejected");
        rejects([&] { parse(text + "\t0\tfield\tvalue\n"); }, "empty table rejected");
        rejects([&] { parse(text + "Character\t\tfield\tvalue\n"); }, "empty row rejected");
        rejects([&] { parse(text + "Character\t0\t\tvalue\n"); }, "empty field rejected");
        rejects([&] { parse(text + "Character\t0\tfield\tbad\\xescape\n"); }, "unknown escape rejected");
        rejects([&] { parse(text + "Character\t0\tfield\tbad\\\n"); }, "dangling escape rejected");
        rejects([&] { parse(text + "Character\t0\tfield\tbad\rvalue\n"); }, "raw carriage return rejected");
        rejects([&] { parse(text + "Character\t0\tfield\t" + std::string(1, '\0') + "\n"); }, "embedded NUL rejected");
        rejects([&] { parse(text + "Character\t0\tfield\t\xC0\xAF\n"); }, "overlong UTF-8 rejected");
        rejects([&] { parse(text + "Character\t0\tfield\t\xED\xA0\x80\n"); }, "UTF-8 surrogate rejected");
        rejects([&] { parse(text + "Character\t0\tfield\t\xF4\x90\x80\x80\n"); }, "UTF-8 out of range rejected");
        rejects([&] { parse(text + "Character\t0\tfield\t\xE3\x81\n"); }, "truncated UTF-8 rejected");
        std::istringstream broken(text);
        broken.setstate(std::ios::badbit);
        rejects([&] { er::Catalog::read(broken); }, "stream failure rejected");
    } catch (const std::exception& error) {
        check(false, std::string("unexpected error: ") + error.what());
    }
    std::cout << "catalog checks=" << checks << ", failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
