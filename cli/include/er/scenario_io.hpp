#pragma once

#include "er/calculator.hpp"
#include <iosfwd>

namespace er {
Scenario read_scenario(std::istream& input);
void write_text(std::ostream& output, const Scenario& scenario, const Result& result);
void write_json(std::ostream& output, const Scenario& scenario, const Result& result);
} // namespace er
