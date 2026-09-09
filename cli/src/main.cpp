#include "er/scenario_io.hpp"
#include "er/named_cli.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
void usage(std::ostream& output) {
    output << "Usage: er_calc [--interactive] [--loadout names.ini] [--catalog data/catalog.tsv]\n"
           << "       er_calc --loadout names.ini [--format text|json] [--output path]\n"
           << "       er_calc --list characters|weapons|armor|traits [--search name]\n"
           << "       er_calc <scenario.ini> [--format text|json] [--output path]\n"
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
        std::optional<std::filesystem::path> named_path;
        std::optional<std::filesystem::path> catalog_path;
        std::optional<std::filesystem::path> output_path;
        std::string list_kind, search;
        bool interactive = argc == 1;
        bool interactive_seen = false, search_seen = false;
        bool json = false;
        bool format_seen = false;
        for (int i = 1; i < argc; ++i) {
            const std::filesystem::path argument(argv[i]);
            if (argument == "--interactive") {
                if (interactive_seen) throw std::invalid_argument("duplicate --interactive");
                interactive_seen = interactive = true;
            } else if (argument == "--loadout" || argument == "--catalog") {
                auto& destination = argument == "--loadout" ? named_path : catalog_path;
                if (destination || ++i == argc) throw std::invalid_argument("--loadout/--catalog requires one path");
                destination = std::filesystem::path(argv[i]);
                if (destination->empty()) throw std::invalid_argument("path must not be empty");
            } else if (argument == "--list" || argument == "--search") {
                const bool is_list = argument == "--list";
                if ((is_list ? !list_kind.empty() : search_seen) || ++i == argc) throw std::invalid_argument("--list/--search requires one value");
                const auto bytes = std::filesystem::path(argv[i]).u8string();
                (is_list ? list_kind : search) = std::string(bytes.begin(), bytes.end());
                if (!is_list) search_seen = true;
            } else if (argument == "--format") {
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
        if (input_path && (named_path || catalog_path || interactive || !list_kind.empty()))
            throw std::invalid_argument("manual scenario cannot be combined with named catalog mode");
        if (!list_kind.empty() && (named_path || interactive || output_path || format_seen))
            throw std::invalid_argument("--list cannot be combined with calculation/output options");
        if (search_seen && list_kind.empty()) throw std::invalid_argument("--search requires --list");
        if (interactive && (output_path || format_seen)) throw std::invalid_argument("interactive mode saves results using menu 9");
        if (!input_path && !named_path && !interactive && list_kind.empty()) {
            usage(std::cerr);
            return 2;
        }
        if (!input_path && !catalog_path) {
            const auto executable = std::filesystem::absolute(std::filesystem::path(argv[0]));
            const std::vector<std::filesystem::path> candidates{
                executable.parent_path() / "../../data/catalog.tsv",
                executable.parent_path() / "../data/catalog.tsv",
                std::filesystem::path("data/catalog.tsv")};
            for (const auto& candidate : candidates) if (std::filesystem::is_regular_file(candidate)) { catalog_path = candidate; break; }
            if (!catalog_path) throw std::runtime_error("data/catalog.tsv not found; use --catalog path");
        }
        if (output_path) {
            const auto output_absolute = std::filesystem::absolute(*output_path).lexically_normal();
            for (const auto& protected_path : {input_path, named_path, catalog_path}) {
                if (!protected_path) continue;
                const auto input_absolute = std::filesystem::absolute(*protected_path).lexically_normal();
                if (input_absolute == output_absolute ||
                    (std::filesystem::exists(*output_path) && std::filesystem::equivalent(*protected_path, *output_path)))
                    throw std::invalid_argument("output path must differ from input/catalog path");
            }
        }
        er::Scenario scenario;
        std::optional<er::ResolvedExperiment> resolved;
        if (input_path) {
            std::ifstream input(*input_path, std::ios::binary);
            if (!input) throw std::runtime_error("cannot open scenario input file");
            scenario = er::read_scenario(input);
        } else {
            std::ifstream source(*catalog_path, std::ios::binary);
            if (!source) throw std::runtime_error("cannot open catalog file");
            const auto catalog = er::Catalog::read(source);
            if (!list_kind.empty()) { er::list_catalog(std::cout, catalog, list_kind, search); return 0; }
            er::NamedExperiment named;
            if (named_path) {
                std::ifstream input(*named_path, std::ios::binary);
                if (!input) throw std::runtime_error("cannot open named loadout file");
                named = er::read_named_experiment(input);
            }
            if (interactive) {
                const auto repo = std::filesystem::absolute(*catalog_path).lexically_normal().parent_path().parent_path();
                return er::interactive_experiment(catalog, named, repo / "outputs", std::cin, std::cout);
            }
            resolved = er::resolve_experiment(catalog, named);
            scenario = resolved->scenario;
            if (json && !resolved->blockers.empty()) throw std::invalid_argument("unsupported effects; use text output to inspect the selected stats and blockers");
        }
        // Render completely before opening the output, so invalid input cannot truncate an earlier result.
        std::ostringstream rendered;
        if (resolved && !json) er::write_named_report(rendered, *resolved);
        else {
            const auto result = er::simulate(scenario);
            if (json) er::write_json(rendered, scenario, result);
            else er::write_text(rendered, scenario, result);
        }
        auto write = [&](std::ostream& output) {
            output << rendered.str();
            output.flush();
            if (!output) throw std::runtime_error("failed to flush result output");
        };
        if (output_path) {
            if (!output_path->parent_path().empty()) std::filesystem::create_directories(output_path->parent_path());
            std::ofstream output(*output_path, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("cannot open result output file");
            write(output);
            output.close();
            if (!output) throw std::runtime_error("failed to close result output file");
        } else {
            write(std::cout);
        }
        return resolved && !resolved->blockers.empty() ? 3 : 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    return run(argc, argv);
}
#else
int main(int argc, char* argv[]) { return run(argc, argv); }
#endif
