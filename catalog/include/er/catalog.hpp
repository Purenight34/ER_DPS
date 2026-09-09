#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace er {

// Raw, versioned game records. This layer performs no game-stat calculations.
struct CatalogRecord {
    std::string table;
    std::string row_id;
    std::map<std::string, std::string> fields;

    const std::string& require(std::string_view key) const;
    std::string value(std::string_view key, std::string fallback = "") const;
    double number(std::string_view key) const;
};

class Catalog {
public:
    static Catalog read(std::istream& input);

    const std::vector<CatalogRecord>& records() const;
    std::vector<const CatalogRecord*> table(std::string_view table_name) const;
    // Exact, case-sensitive UTF-8 display-name or official-code match.
    // Missing or ambiguous input raises an error; no record is chosen implicitly.
    const CatalogRecord& find(std::string_view table_name, std::string_view name_or_code) const;
    // Substring search of display names and official codes, in source row order.
    std::vector<const CatalogRecord*> search(std::string_view table_name, std::string_view query) const;
    const CatalogRecord& metadata() const;

private:
    std::vector<CatalogRecord> records_;
};

} // namespace er
