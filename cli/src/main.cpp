#include "er/scenario_io.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
void usage(std::ostream& output) {
    output << "Usage: er_calc <scenario.ini> [--format text|json] [--output path]\n"
           << "       er_calc --help\n"
           << "Reads explicit UTF-8 experiment inputs; uses a user-approved temporary model.\n"
           << "Results go to stdout unless --output is specified (existing output is overwritten).\n";
}

template <typename Character>
int run(int argc, Character* argv[]) {
    try {
        if (argc == 2 && std::filesystem::path(argv[1]) == "--help") {
            usage(std::cout);
            return 0;
        }
        std::optional<std::filesystem::path> input_path;
        std::optional<std::filesystem::path> output_path;
        bool json = false;
        bool format_seen = false;
        for (int i = 1; i < argc; ++i) {
            const std::filesystem::path argument(argv[i]);
            if (argument == "--format") {
                if (format_seen || ++i == argc) throw std::invalid_argument("--format requires one value and may occur only once");
                format_seen = true;
                const std::filesystem::path value(argv[i]);
                if (value != "text" && value != "json") throw std::invalid_argument("--format must be text or json");
                json = value == "json";
            } else if (argument == "--output") {
                if (output_path || ++i == argc) throw std::invalid_argument("--output requires one path and may occur only once");
                output_path = std::filesystem::path(argv[i]);
                if (output_path->empty()) throw std::invalid_argument("output path must be nonempty");
            } else {
                if (argument.empty() || argument.native().front() == static_cast<Character>('-'))
                    throw std::invalid_argument("unknown option; use --help");
                if (input_path) throw std::invalid_argument("only one scenario input path is allowed");
                input_path = argument;
            }
        }
        if (!input_path) {
            usage(std::cerr);
            return 2;
        }
        if (output_path) {
            const auto input_absolute = std::filesystem::absolute(*input_path).lexically_normal();
            const auto output_absolute = std::filesystem::absolute(*output_path).lexically_normal();
            if (input_absolute == output_absolute ||
                (std::filesystem::exists(*output_path) && std::filesystem::equivalent(*input_path, *output_path)))
                throw std::invalid_argument("output path must differ from input path");
        }
        std::ifstream input(*input_path, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open scenario input file");
        const auto scenario = er::read_scenario(input);
        const auto result = er::simulate(scenario);
        auto write = [&](std::ostream& output) {
            if (json) er::write_json(output, scenario, result);
            else er::write_text(output, scenario, result);
            output.flush();
            if (!output) throw std::runtime_error("failed to flush result output");
        };
        if (output_path) {
            std::ofstream output(*output_path, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("cannot open result output file");
            write(output);
            output.close();
            if (!output) throw std::runtime_error("failed to close result output file");
        } else {
            write(std::cout);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) { return run(argc, argv); }
#else
int main(int argc, char* argv[]) { return run(argc, argv); }
#endif
