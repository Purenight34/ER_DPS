#pragma once
#include "er/loadout.hpp"
#include <filesystem>
#include <iosfwd>

namespace er {
NamedExperiment read_named_experiment(std::istream& input);
void write_named_experiment(std::ostream& output, const NamedExperiment& input);
void list_catalog(std::ostream& output, const Catalog& catalog,
                  const std::string& kind, const std::string& query);
void write_named_report(std::ostream& output, const ResolvedExperiment& resolved);
int interactive_experiment(const Catalog& catalog, NamedExperiment input,
                           const std::filesystem::path& save_directory,
                           std::istream& console_input, std::ostream& output);
} // namespace er
