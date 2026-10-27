// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>

#ifdef _WIN32
#include <boost/nowide/args.hpp>
#endif

#include "exporter.hpp"

namespace {

void help() {
    std::cout << "ark_config_tool: offline Entity field-table and business-data exporter\n"
                 "  --workbook FILE.xlsx --output NEW_DIRECTORY\n"
                 "  --schema fields.csv [--schema-sheet SHEET] --data Class=data.csv --output NEW_DIRECTORY\n"
                 "  --input entity_schema.json --output NEW_DIRECTORY\n"
                 "  --validate entity_schema.json\n"
                 "Options: --data Class=FILE may repeat; --replace replaces only a tool-owned bundle;\n"
                 "         --no-code emits runtime JSON without C++/C# field-index definitions.\n"
                 "A combined workbook requires a 'fields' sheet; other sheets contain data for their class names.\n"
                 "XLSX formula cells are not evaluated; export calculated values or CSV instead.\n";
}

} // namespace

int main(int argc, char** argv) {
    try
    {
#ifdef _WIN32
        boost::nowide::args utf8_arguments(argc, argv);
#endif
        std::map<std::string, std::string> options;
        std::map<std::string, std::string> data;
        bool replace = false, code = true;
        for (int i = 1; i < argc; ++i)
        {
            const std::string argument = argv[i];
            if (argument == "--help" || argument == "-h")
            {
                help();
                return 0;
            }
            if (argument == "--replace" || argument == "--no-code")
            {
                if (argument == "--replace")
                {
                    replace = true;
                }
                else
                {
                    code = false;
                }
                continue;
            }
            if (argument != "--workbook" && argument != "--schema" && argument != "--schema-sheet" &&
                argument != "--data" && argument != "--input" && argument != "--output" && argument != "--validate")
            {
                throw std::invalid_argument("unknown option " + argument);
            }
            if (++i == argc)
            {
                throw std::invalid_argument("missing value for " + argument);
            }
            const std::string value = argv[i];
            if (argument == "--data")
            {
                const auto equal = value.find('=');
                if (equal == 0 || equal == std::string::npos || equal + 1 == value.size() ||
                    !data.emplace(value.substr(0, equal), value.substr(equal + 1)).second)
                {
                    throw std::invalid_argument("expected a unique --data Class=FILE");
                }
            }
            else if (!options.emplace(argument, value).second)
            {
                throw std::invalid_argument("duplicate option " + argument);
            }
        }
        if (options.contains("--validate"))
        {
            if (options.size() != 1 || !data.empty() || replace || !code)
            {
                throw std::invalid_argument("--validate must be used on its own");
            }
            const auto document = ark::entity_json::load(options.at("--validate"));
            std::cout << "Valid Entity JSON: " << document.classes.size() << " classes, " << document.configs.size()
                      << " configuration tables\n";
            return 0;
        }
        const auto modes = static_cast<int>(options.contains("--workbook")) +
                           static_cast<int>(options.contains("--schema")) +
                           static_cast<int>(options.contains("--input"));
        if (modes != 1 || !options.contains("--output") ||
            (!options.contains("--schema") && (options.contains("--schema-sheet") || !data.empty())))
        {
            throw std::invalid_argument("choose one input mode and an --output directory; use --help");
        }
        ark::entity_json::document document;
        if (options.contains("--input"))
        {
            document = ark::entity_json::load(options.at("--input"));
        }
        else if (options.contains("--schema"))
        {
            document = ark::entity_export::read_fields(
                ark::entity_export::read_table(ark::entity_export::utf8_path(options.at("--schema")),
                    options.contains("--schema-sheet") ? options.at("--schema-sheet") : ""));
            for (const auto& [name, path] : data)
            {
                ark::entity_export::add_config(
                    document, name, ark::entity_export::read_table(ark::entity_export::utf8_path(path)));
            }
        }
        else
        {
            auto sheets = ark::entity_export::read_workbook(ark::entity_export::utf8_path(options.at("--workbook")));
            const auto fields = sheets.find("fields");
            if (fields == sheets.end())
            {
                throw std::runtime_error("combined workbook requires a worksheet named fields");
            }
            document = ark::entity_export::read_fields(fields->second);
            for (const auto& [name, sheet] : sheets)
            {
                if (name != "fields")
                {
                    ark::entity_export::add_config(document, name, sheet);
                }
            }
        }
        ark::entity_export::export_bundle(
            document, ark::entity_export::utf8_path(options.at("--output")), replace, code);
        std::cout << "Exported " << document.classes.size() << " classes and " << document.configs.size()
                  << " configuration tables to " << options.at("--output") << '\n';
        return 0;
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << "Usage error: " << error.what() << '\n';
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Export failed: " << error.what() << '\n';
        return 1;
    }
}
