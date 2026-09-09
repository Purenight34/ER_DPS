#include "er/catalog.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <istream>
#include <stdexcept>
#include <utility>

namespace er {
namespace {

[[noreturn]] void invalid_line(std::size_t line, std::string_view reason) {
    throw std::runtime_error("catalog line " + std::to_string(line) + ": " + std::string(reason));
}

void validate_utf8(std::string_view text, std::size_t line) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            if (lead == 0 || lead == '\r') invalid_line(line, "unescaped control character");
            ++i;
            continue;
        }
        std::size_t width = 0;
        std::uint32_t codepoint = 0;
        std::uint32_t minimum = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            width = 2;
            codepoint = lead & 0x1F;
            minimum = 0x80;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            width = 3;
            codepoint = lead & 0x0F;
            minimum = 0x800;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            width = 4;
            codepoint = lead & 0x07;
            minimum = 0x10000;
        } else {
            invalid_line(line, "invalid UTF-8 leading byte");
        }
        if (text.size() - i < width) invalid_line(line, "truncated UTF-8 sequence");
        for (std::size_t offset = 1; offset < width; ++offset) {
            const auto byte = static_cast<unsigned char>(text[i + offset]);
            if ((byte & 0xC0) != 0x80) invalid_line(line, "invalid UTF-8 continuation byte");
            codepoint = (codepoint << 6) | (byte & 0x3F);
        }
        if (codepoint < minimum || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            invalid_line(line, "invalid UTF-8 codepoint");
        }
        i += width;
    }
}

std::string unescape(std::string_view text, std::size_t line) {
    std::string decoded;
    decoded.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\') {
            decoded += text[i];
            continue;
        }
        if (++i == text.size()) invalid_line(line, "incomplete escape sequence");
        switch (text[i]) {
        case '\\': decoded += '\\'; break;
        case 't': decoded += '\t'; break;
        case 'n': decoded += '\n'; break;
        case 'r': decoded += '\r'; break;
        default: invalid_line(line, "unknown escape sequence");
        }
    }
    return decoded;
}

std::array<std::string, 4> columns(std::string_view text, std::size_t line) {
    std::array<std::string, 4> result;
    std::size_t start = 0;
    for (std::size_t column = 0; column < result.size(); ++column) {
        const auto end = text.find('\t', start);
        if ((column < 3 && end == std::string_view::npos) ||
            (column == 3 && end != std::string_view::npos)) {
            invalid_line(line, "expected exactly four TSV columns");
        }
        const auto length = end == std::string_view::npos ? text.size() - start : end - start;
        result[column] = unescape(text.substr(start, length), line);
        if (column < 3 && result[column].empty()) invalid_line(line, "empty table, row ID, or field");
        if (end != std::string_view::npos) start = end + 1;
    }
    return result;
}

std::string identity(const CatalogRecord& record) {
    return record.table + " row " + record.row_id;
}

bool exact_match(const CatalogRecord& record, std::string_view query) {
    for (const auto* field : {"code", "_name"}) {
        const auto it = record.fields.find(field);
        if (it != record.fields.end() && it->second == query) return true;
    }
    return false;
}

bool substring_match(const CatalogRecord& record, std::string_view query) {
    if (query.empty()) return true;
    for (const auto* field : {"code", "_name"}) {
        const auto it = record.fields.find(field);
        if (it != record.fields.end() && it->second.find(query) != std::string::npos) return true;
    }
    return false;
}

} // namespace

const std::string& CatalogRecord::require(std::string_view key) const {
    const auto it = fields.find(std::string(key));
    if (it == fields.end()) {
        throw std::runtime_error(identity(*this) + ": missing field " + std::string(key));
    }
    return it->second;
}

std::string CatalogRecord::value(std::string_view key, std::string fallback) const {
    const auto it = fields.find(std::string(key));
    return it == fields.end() ? std::move(fallback) : it->second;
}

double CatalogRecord::number(std::string_view key) const {
    const auto& text = require(key);
    double result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(result)) {
        throw std::runtime_error(identity(*this) + ": invalid finite number in field " + std::string(key));
    }
    return result;
}

Catalog Catalog::read(std::istream& input) {
    Catalog catalog;
    std::string line;
    if (!std::getline(input, line)) throw std::runtime_error("catalog: missing schema header or failed input stream");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
    if (line != "# er-catalog-v1") throw std::runtime_error("catalog: expected # er-catalog-v1 schema header");

    std::map<std::pair<std::string, std::string>, std::size_t> row_indices;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        validate_utf8(line, line_number);
        auto values = columns(line, line_number);
        const auto key = std::make_pair(values[0], values[1]);
        const auto [row, inserted] = row_indices.emplace(key, catalog.records_.size());
        if (inserted) catalog.records_.push_back({std::move(values[0]), std::move(values[1]), {}});
        auto& record = catalog.records_[row->second];
        if (!record.fields.emplace(values[2], std::move(values[3])).second) {
            invalid_line(line_number, "duplicate field " + values[2] + " in " + identity(record));
        }
    }
    if (input.bad() || (input.fail() && !input.eof())) throw std::runtime_error("catalog: input stream read failed");
    const auto& meta = catalog.metadata();
    for (const auto* field : {"data_version", "patch_version", "source"}) {
        if (meta.require(field).empty()) throw std::runtime_error("catalog: empty metadata field " + std::string(field));
    }
    return catalog;
}

const std::vector<CatalogRecord>& Catalog::records() const {
    return records_;
}

std::vector<const CatalogRecord*> Catalog::table(std::string_view table_name) const {
    std::vector<const CatalogRecord*> result;
    for (const auto& record : records_) {
        if (record.table == table_name) result.push_back(&record);
    }
    return result;
}

const CatalogRecord& Catalog::find(std::string_view table_name, std::string_view name_or_code) const {
    if (name_or_code.empty()) throw std::runtime_error("catalog: empty lookup in table " + std::string(table_name));
    std::vector<const CatalogRecord*> matches;
    for (const auto& record : records_) {
        if (record.table == table_name && exact_match(record, name_or_code)) matches.push_back(&record);
    }
    if (matches.empty()) {
        throw std::runtime_error("catalog: no record in " + std::string(table_name) + " for '" + std::string(name_or_code) + "'");
    }
    if (matches.size() > 1) {
        std::string message = "catalog: ambiguous name or code '" + std::string(name_or_code) + "' in " +
                              std::string(table_name) + "; candidates:";
        for (const auto* record : matches) {
            message += " code=" + record->value("code", "(missing)") + " row=" + record->row_id;
        }
        throw std::runtime_error(message);
    }
    return *matches.front();
}

std::vector<const CatalogRecord*> Catalog::search(std::string_view table_name, std::string_view query) const {
    std::vector<const CatalogRecord*> result;
    for (const auto& record : records_) {
        if (record.table == table_name && substring_match(record, query)) result.push_back(&record);
    }
    return result;
}

const CatalogRecord& Catalog::metadata() const {
    for (const auto& record : records_) {
        if (record.table == "Meta" && record.row_id == "0") return record;
    }
    throw std::runtime_error("catalog: missing Meta row 0");
}

} // namespace er
