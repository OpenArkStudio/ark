// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>

#include "../network/doctest.h"

#include <ark/application_manifest.hpp>

namespace {

constexpr std::array<std::string_view, 24> migrated_application_presets = {"db.app.toml", "dir.app.toml",
    "entity_operations.app.toml", "game.app.toml", "http_client_sample.app.toml", "http_server.app.toml",
    "login.app.toml", "master.app.toml", "module_lifecycle.app.toml", "mysql_sample.app.toml", "oss.app.toml",
    "physic.app.toml", "physics_scene.app.toml", "proxy.app.toml", "realm.example.app.toml", "realm.game.app.toml",
    "realm.master.app.toml", "redis_sample.app.toml", "remote_log.app.toml", "router.app.toml", "simulate.app.toml",
    "terraria.game.app.toml", "timers.app.toml", "world.app.toml"};
constexpr std::array<std::string_view, 1> native_application_presets = {"guest_room.app.toml"};

std::string utf8_path(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {encoded.begin(), encoded.end()};
}

std::filesystem::path native_path(std::string_view path) {
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

class manifest_files
{
public:
    manifest_files()
        : directory_(std::filesystem::temp_directory_path() /
                     ("ark-manifest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        if (!std::filesystem::create_directory(directory_))
        {
            throw std::runtime_error("could not create temporary manifest directory");
        }
    }

    ~manifest_files() {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    std::string path(std::string_view name) const {
        return utf8_path(directory_ / native_path(name));
    }

    std::string write(std::string_view contents, std::string_view name = "sample.app.toml") const {
        const auto filename = path(name);
        std::ofstream output(native_path(filename), std::ios::binary);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.close();
        return filename;
    }

private:
    std::filesystem::path directory_;
};

void check_error(const std::string& path, bool has_position = true) {
    try
    {
        static_cast<void>(ark::load_application_manifest(path));
        DOCTEST_FAIL("invalid manifest was accepted");
    }
    catch (const ark::application_manifest_error& error)
    {
        DOCTEST_CHECK(error.path() == path);
        DOCTEST_CHECK(std::string(error.what()).find(path) != std::string::npos);
        DOCTEST_CHECK((error.line() > 0) == has_position);
        DOCTEST_CHECK((error.column() > 0) == has_position);
    }
}

} // namespace

DOCTEST_TEST_CASE("TOML and XML samples produce equivalent owning application manifests") {
    const auto base = std::filesystem::path(ARK_TEST_SOURCE_DIR) / "config";
    const auto toml = ark::load_application_manifest(utf8_path(base / "timers.app.toml"));
    const auto xml = ark::load_application_manifest(
        utf8_path(base.parent_path() / "tests/core/fixtures/application_presets/timers.app.conf"));
    DOCTEST_CHECK(toml.plugin_library_dir == "bin/");
    DOCTEST_CHECK(toml.plugin_config_dir == "conf/");
    DOCTEST_REQUIRE(toml.plugins.size() == 2);
    DOCTEST_CHECK(toml.plugins.at("ark_utility_plugin").empty());
    DOCTEST_CHECK(toml.plugins.at("timers_plugin").empty());
    DOCTEST_CHECK(toml.plugin_library_dir == xml.plugin_library_dir);
    DOCTEST_CHECK(toml.plugin_config_dir == xml.plugin_config_dir);
    DOCTEST_CHECK(toml.plugins == xml.plugins);
}

namespace {

void check_equivalent_manifest(const ark::application_manifest& actual, const ark::application_manifest& legacy) {
    DOCTEST_CHECK(actual.plugin_library_dir == legacy.plugin_library_dir);
    DOCTEST_CHECK(actual.plugin_config_dir == legacy.plugin_config_dir);
    DOCTEST_CHECK(actual.plugins == legacy.plugins);
    // Retain the same insertion sequence and resulting traversal on this STL.
    // An unordered map does not establish a public lifecycle-order contract.
    DOCTEST_CHECK(
        std::equal(actual.plugins.begin(), actual.plugins.end(), legacy.plugins.begin(), legacy.plugins.end()));
}

} // namespace

DOCTEST_TEST_CASE("migrated built-in TOML presets retain their legacy XML application schema") {
    const auto root = std::filesystem::path(ARK_TEST_SOURCE_DIR);
    size_t count = 0;
    for (const auto name : migrated_application_presets)
    {
        DOCTEST_CAPTURE(name);
        auto legacy = std::filesystem::path(name);
        legacy.replace_extension(".conf");
        check_equivalent_manifest(ark::load_application_manifest(utf8_path(root / "config" / name)),
            ark::load_application_manifest(utf8_path(root / "tests/core/fixtures/application_presets" / legacy)));
        ++count;
    }
    DOCTEST_CHECK(count == 24);
}

DOCTEST_TEST_CASE("native guest-room TOML declares its complete plugin configuration without legacy XML") {
    const auto root = std::filesystem::path(ARK_TEST_SOURCE_DIR);
    const auto manifest = ark::load_application_manifest(utf8_path(root / "config/guest_room.app.toml"));
    DOCTEST_CHECK(manifest.plugin_library_dir == "bin/");
    DOCTEST_CHECK(manifest.plugin_config_dir == "conf/");
    DOCTEST_REQUIRE(manifest.plugins.size() == 5);
    DOCTEST_CHECK(manifest.plugins.at("ark_utility_plugin").empty());
    DOCTEST_CHECK(manifest.plugins.at("ark_kernel_plugin") == "kernel.plugin.conf");
    DOCTEST_CHECK(manifest.plugins.at("ark_bus_plugin") == "guest_room.bus.toml");
    DOCTEST_CHECK(manifest.plugins.at("ark_net_plugin").empty());
    DOCTEST_CHECK(manifest.plugins.at("guest_room_plugin").empty());
}

DOCTEST_TEST_CASE("every built-in TOML application preset has an explicit migrated or native classification") {
    const auto root = std::filesystem::path(ARK_TEST_SOURCE_DIR);
    std::set<std::string> expected;
    for (const auto name : migrated_application_presets)
    {
        DOCTEST_CAPTURE(name);
        DOCTEST_CHECK(expected.emplace(name).second);
    }
    for (const auto name : native_application_presets)
    {
        DOCTEST_CAPTURE(name);
        DOCTEST_CHECK(expected.emplace(name).second);
    }
    std::set<std::string> actual;
    for (const auto& entry : std::filesystem::directory_iterator(root / "config"))
    {
        const auto name = entry.path().filename().string();
        if (name.ends_with(".app.toml"))
        {
            DOCTEST_CAPTURE(name);
            DOCTEST_CHECK(expected.contains(name));
            actual.insert(name);
        }
    }
    for (const auto& name : expected)
    {
        DOCTEST_CAPTURE(name);
        DOCTEST_CHECK(actual.contains(name));
    }
    DOCTEST_CHECK(actual == expected);
}

DOCTEST_TEST_CASE("deployment TOML application payloads retain their legacy XML schema") {
    const auto root = std::filesystem::path(ARK_TEST_SOURCE_DIR);
    std::ifstream input(root / "deployment/photon_config.yaml");
    DOCTEST_REQUIRE(input.is_open());
    manifest_files files;
    size_t count = 0;
    bool has_data = false;
    bool has_kind = false;
    bool has_metadata = false;
    std::string line;
    while (std::getline(input, line))
    {
        has_data = has_data || line == "data:";
        has_kind = has_kind || line == "kind: ConfigMap";
        has_metadata = has_metadata || line == "metadata:";
        if (!line.starts_with("  ") || !line.ends_with(".app.toml: |-"))
        {
            continue;
        }
        const auto name = line.substr(2, line.size() - 2 - 4);
        DOCTEST_CAPTURE(name);
        std::string payload;
        while (input.peek() == ' ' || input.peek() == '\n')
        {
            const auto position = input.tellg();
            std::getline(input, line);
            if (!line.empty() && !line.starts_with("    "))
            {
                input.seekg(position);
                break;
            }
            payload += (line.empty() ? "" : line.substr(4)) + "\n";
        }
        auto legacy = std::filesystem::path(name);
        legacy.replace_extension(".conf");
        check_equivalent_manifest(ark::load_application_manifest(files.write(payload, name)),
            ark::load_application_manifest(utf8_path(root / "tests/core/fixtures/application_presets" / legacy)));
        ++count;
    }
    DOCTEST_CHECK(count == 21);
    DOCTEST_CHECK(has_data);
    DOCTEST_CHECK(has_kind);
    DOCTEST_CHECK(has_metadata);
}

DOCTEST_TEST_CASE("XML entities and TOML strings preserve literal configuration characters") {
    manifest_files files;
    const auto xml = ark::load_application_manifest(files.write(
        R"(<xml><plugins plugin_lib_dir="bin &amp; &quot;quoted&quot;\" plugin_conf_dir="配置/#' ">
<plugin name="plugin &amp; &quot;quoted&quot;" conf="settings/#' &amp; &lt;literal&gt;.conf"/>
</plugins></xml>)",
        "characters.app.conf"));
    const auto toml = ark::load_application_manifest(files.write(
        R"([application]
plugin_library_dir = 'bin & "quoted"\'
plugin_config_dir = "配置/#' "
[[plugins]]
name = 'plugin & "quoted"'
config = "settings/#' & <literal>.conf"
)"));
    check_equivalent_manifest(toml, xml);
    DOCTEST_CHECK(toml.plugin_library_dir == R"(bin & "quoted"\)");
    DOCTEST_CHECK(toml.plugins.at(R"(plugin & "quoted")") == "settings/#' & <literal>.conf");
}

DOCTEST_TEST_CASE("TOML preserves directory prefixes and optional empty plugin configurations") {
    ark::application_manifest manifest;
    {
        manifest_files files;
        manifest = ark::load_application_manifest(files.write(R"([application]
plugin_library_dir = 'relative.with.dots/bin'
plugin_config_dir = ''
[[plugins]]
name = 'FirstPlugin'
[[plugins]]
name = 'SecondPlugin'
config = ''
[[plugins]]
name = 'ThirdPlugin'
config = 'nested/service.xml'
)"));
    }
    DOCTEST_CHECK(manifest.plugin_library_dir == "relative.with.dots/bin");
    DOCTEST_CHECK(manifest.plugin_config_dir.empty());
    DOCTEST_REQUIRE(manifest.plugins.size() == 3);
    DOCTEST_CHECK(manifest.plugins.at("FirstPlugin").empty());
    DOCTEST_CHECK(manifest.plugins.at("SecondPlugin").empty());
    DOCTEST_CHECK(manifest.plugins.at("ThirdPlugin") == "nested/service.xml");
}

DOCTEST_TEST_CASE("Missing directories and omitted or empty plugin arrays retain empty manifest semantics") {
    manifest_files files;
    const std::array<std::string_view, 3> inputs = {
        "[application]\n", "plugins = []\n[application]\n", "<xml><plugins/></xml>"};
    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        const auto manifest = ark::load_application_manifest(
            files.write(inputs[index], index == 2 ? "empty.app.conf" : "empty.app.toml"));
        DOCTEST_CHECK(manifest.plugin_library_dir.empty());
        DOCTEST_CHECK(manifest.plugin_config_dir.empty());
        DOCTEST_CHECK(manifest.plugins.empty());
    }
}

DOCTEST_TEST_CASE("XML retains legacy attribute names optional conf and unknown attributes") {
    manifest_files files;
    const auto manifest = ark::load_application_manifest(files.write(
        R"(<xml><plugins plugin_lib_dir="bin/" plugin_conf_dir="../conf/" ignored="yes">
<plugin name="FirstPlugin"/><plugin name="SecondPlugin" conf="" extra="yes"/>
</plugins></xml>)",
        "legacy.app.conf"));
    DOCTEST_CHECK(manifest.plugin_library_dir == "bin/");
    DOCTEST_CHECK(manifest.plugin_config_dir == "../conf/");
    DOCTEST_REQUIRE(manifest.plugins.size() == 2);
    DOCTEST_CHECK(manifest.plugins.at("FirstPlugin").empty());
    DOCTEST_CHECK(manifest.plugins.at("SecondPlugin").empty());
}

DOCTEST_TEST_CASE("TOML validates required tables exact field types and plugin names") {
    manifest_files files;
    const std::array<std::string_view, 14> invalid = {"", "plugins = []\n", "application = 7\n", "application = []\n",
        "[application]\nplugin_library_dir = 1\n", "[application]\nplugin_config_dir = false\n",
        "plugins = 'bad'\n[application]\n", "plugins = [1]\n[application]\n", "[plugins]\n[application]\n",
        "[application]\n[[plugins]]\n", "[application]\n[[plugins]]\nname = ''\n",
        "[application]\n[[plugins]]\nname = 42\n", "[application]\n[[plugins]]\nname = 'Plugin'\nconfig = []\n",
        "[application]\n[[plugins]]\nname = 'Plugin'\nconfig = {}\n"};
    for (const auto input : invalid)
    {
        DOCTEST_CAPTURE(input);
        check_error(files.write(input));
    }
}

DOCTEST_TEST_CASE("TOML rejects unknown fields at every schema level") {
    manifest_files files;
    const std::array<std::string_view, 4> invalid = {"typo = 1\n[application]\n",
        "[application]\nplugin_lib_dir = 'bin/'\n", "[application]\n[[plugins]]\nname = 'Plugin'\nconf = ''\n",
        "[application]\n[extra]\n"};
    for (const auto input : invalid)
    {
        check_error(files.write(input));
    }
}

DOCTEST_TEST_CASE("Both manifest formats reject duplicate and empty plugin names before returning a result") {
    manifest_files files;
    const std::array<std::string_view, 4> invalid = {
        "[application]\n[[plugins]]\nname = 'Plugin'\n[[plugins]]\nname = 'Plugin'\n",
        "<xml><plugins><plugin name='Plugin'/><plugin name='Plugin'/></plugins></xml>",
        "<xml><plugins><plugin/></plugins></xml>", "<xml><plugins><plugin name=''/></plugins></xml>"};
    for (std::size_t index = 0; index < invalid.size(); ++index)
    {
        check_error(files.write(invalid[index], index == 0 ? "duplicate.toml" : "invalid.app.conf"));
    }
    auto manifest = ark::load_application_manifest(files.write("[application]\n[[plugins]]\nname = 'Original'\n"));
    DOCTEST_CHECK_THROWS_AS(
        manifest = ark::load_application_manifest(files.write(invalid[0])), ark::application_manifest_error);
    DOCTEST_REQUIRE(manifest.plugins.size() == 1);
    DOCTEST_CHECK(manifest.plugins.contains("Original"));
}

DOCTEST_TEST_CASE("TOML and XML syntax errors have source positions without disclosing configuration values") {
    manifest_files files;
    const std::array<std::string_view, 4> invalid = {"[application]\nplugin_library_dir = SECRET_SENTINEL\n",
        "[application]\n[[plugins]]\nname = 'Plugin'\nname = 'SECRET_SENTINEL'\n",
        "<xml>\n<plugins secret='SECRET_SENTINEL'></Plugins></xml>", "<xml><plugins secret=SECRET_SENTINEL/></xml>"};
    for (std::size_t index = 0; index < invalid.size(); ++index)
    {
        const auto path = files.write(invalid[index], index < 2 ? "syntax.toml" : "syntax.app.conf");
        check_error(path);
        try
        { static_cast<void>(ark::load_application_manifest(path)); }
        catch (const ark::application_manifest_error& error)
        { DOCTEST_CHECK(std::string(error.what()).find("SECRET_SENTINEL") == std::string::npos); }
    }
    const auto path = files.write("[application]\nplugin_library_dir = 7\n");
    try
    {
        static_cast<void>(ark::load_application_manifest(path));
        DOCTEST_FAIL("numeric directory accepted");
    }
    catch (const ark::application_manifest_error& error)
    {
        DOCTEST_CHECK(error.line() == 2);
        DOCTEST_CHECK(error.column() > 0);
    }
}

DOCTEST_TEST_CASE("Manifest suffix dispatch never falls back from TOML to XML") {
    manifest_files files;
    check_error(files.write("<xml><plugins/></xml>"));
    check_error(files.write("[application]\n", "wrong.app.conf"));
    check_error(files.write("[application]\n", "wrong.app.toml.extra"));
    check_error(files.write("<plugins/> unmatched", "empty.app.conf"));
    check_error(files.write("", "empty.app.conf"));
    check_error(files.write("<plugins/>", "direct-root.app.conf"));
}

DOCTEST_TEST_CASE("Manifest strings and input reject raw or decoded embedded NUL") {
    manifest_files files;
    const std::array<std::string_view, 5> decoded = {"[application]\nplugin_library_dir = \"x\\u0000y\"\n",
        "[application]\nplugin_config_dir = \"x\\u0000y\"\n", "[application]\n[[plugins]]\nname = \"x\\u0000y\"\n",
        "[application]\n[[plugins]]\nname = 'Plugin'\nconfig = \"x\\u0000y\"\n",
        "<xml><plugins><plugin name='Plugin' conf='x&#0;y'/></plugins></xml>"};
    for (std::size_t index = 0; index < decoded.size(); ++index)
    {
        check_error(files.write(decoded[index], index == 4 ? "nul.app.conf" : "nul.toml"));
    }
    for (const auto name : {"raw.toml", "raw.app.conf"})
    {
        std::string input = name == std::string_view("raw.toml") ? "[application]\n" : "<xml><plugins/></xml>";
        input += '\0';
        input += "hidden data";
        check_error(files.write(input, name));
    }
    const auto nul_path = files.path("missing") + std::string("\0.toml", 6);
    DOCTEST_CHECK_THROWS_AS(ark::load_application_manifest(nul_path), ark::application_manifest_error);
}

DOCTEST_TEST_CASE("TOML supports UTF8 values and native Unicode filenames and rejects invalid UTF8") {
    manifest_files files;
    const auto path =
        files.write("[application]\nplugin_config_dir = '配置/'\n[[plugins]]\nname = '插件'\n", "配置.app.toml");
    const auto manifest = ark::load_application_manifest(path);
    DOCTEST_CHECK(manifest.plugin_config_dir == "配置/");
    DOCTEST_CHECK(manifest.plugins.contains("插件"));
    std::string invalid = "[application]\nplugin_config_dir = '";
    invalid += static_cast<char>(0xff);
    invalid += "'\n";
    check_error(files.write(invalid));
}

DOCTEST_TEST_CASE("Missing files and unreadable directory inputs fail without partial results") {
    manifest_files files;
    check_error(files.path("missing.toml"), false);
    check_error(files.path("missing.app.conf"), false);
    const auto directory = files.path("directory.toml");
    DOCTEST_REQUIRE(std::filesystem::create_directory(native_path(directory)));
    check_error(directory, false);
}

DOCTEST_TEST_CASE("omitted logging has a usable console INFO default in both formats") {
    manifest_files files;
    for (const auto name : {"default.toml", "default.xml"})
    {
        const auto manifest = ark::load_application_manifest(
            files.write(std::string_view(name).ends_with("toml") ? "[application]\n" : "<xml><plugins/></xml>", name));
        DOCTEST_CHECK(manifest.logging.console);
        DOCTEST_CHECK_FALSE(manifest.logging.file);
        DOCTEST_CHECK(manifest.logging.level == "info");
        DOCTEST_CHECK(manifest.logging.queue_count == 0);
        DOCTEST_CHECK_FALSE(manifest.logging.pattern.empty());
        DOCTEST_CHECK(manifest.logging.loggers.empty());
    }
}

DOCTEST_TEST_CASE("embedded logging owns overrides and named loggers inherit base settings") {
    manifest_files files;
    const auto manifest = ark::load_application_manifest(files.write(R"([application]
[logging]
console = false
file = true
level = 'debug'
pattern = '%v'
output_path = 'logs with spaces/nested'
queue_count = 65535
[logging.loggers.oss]
level = 'warn'
output_path = 'logs/oss'
)"));
    DOCTEST_CHECK_FALSE(manifest.logging.console);
    DOCTEST_CHECK(manifest.logging.file);
    DOCTEST_CHECK(manifest.logging.level == "debug");
    DOCTEST_CHECK(manifest.logging.pattern == "%v");
    DOCTEST_CHECK(manifest.logging.output_path == "logs with spaces/nested");
    DOCTEST_CHECK(manifest.logging.queue_count == 65535);
    const auto& oss = manifest.logging.loggers.at("oss");
    DOCTEST_CHECK(oss.file);
    DOCTEST_CHECK_FALSE(oss.console);
    DOCTEST_CHECK(oss.pattern == "%v");
    DOCTEST_CHECK(oss.level == "warn");
    DOCTEST_CHECK(oss.output_path == "logs/oss");
    const auto xml = ark::load_application_manifest(files.write(
        R"(<xml><plugins/><logging console="false" file="true" level="debug" pattern="%v" output_path="logs with spaces/nested" queue_count="65535"><logger name="oss" level="warn" output_path="logs/oss"/></logging></xml>)",
        "logging.xml"));
    DOCTEST_CHECK(xml.logging.level == manifest.logging.level);
    DOCTEST_CHECK(xml.logging.queue_count == manifest.logging.queue_count);
    DOCTEST_CHECK(xml.logging.loggers.at("oss").output_path == oss.output_path);
}

DOCTEST_TEST_CASE("logging rejects bad fields ranges and disabled outputs before returning a manifest") {
    manifest_files files;
    for (const auto bad : {"logging = 1\n", "[logging]\nconsole = 1\n", "[logging]\nfile = 'yes'\n",
             "[logging]\nlevel = 'SECRET_SENTINEL'\n", "[logging]\nqueue_count = -1\n",
             "[logging]\nqueue_count = 65536\n", "[logging]\nqueue_count = 1.0\n", "[logging]\npattern = ''\n",
             "[logging]\noutput_path = ''\n", "[logging]\noutput_path = \"x\\u0000y\"\n", "[logging]\nunknown = true\n",
             "[logging]\nconsole = false\n", "[logging]\nloggers = []\n", "[logging.loggers]\noss = 1\n",
             "[logging.loggers.oss]\nloggers = {}\n", "[logging.loggers.\"x\\u0000y\"]\nlevel = 'info'\n"})
    {
        DOCTEST_CAPTURE(std::string_view(bad));
        const auto path = files.write(std::string(bad) + "[application]\n");
        check_error(path);
        try
        { static_cast<void>(ark::load_application_manifest(path)); }
        catch (const ark::application_manifest_error& error)
        {
            DOCTEST_CHECK(std::string(error.what()).find("logging") != std::string::npos);
            DOCTEST_CHECK(std::string(error.what()).find("SECRET_SENTINEL") == std::string::npos);
        }
    }
    for (const auto attributes : {"console='yes'", "queue_count='-1'", "level='bad'", "typo='1'"})
    {
        const auto path = files.write(std::string("<xml><plugins/><logging ") + attributes + "/></xml>", "bad.xml");
        DOCTEST_CHECK_THROWS_AS(ark::load_application_manifest(path), ark::application_manifest_error);
    }
}

DOCTEST_TEST_CASE("XML logging rejects duplicate attributes elements and nested logger settings") {
    manifest_files files;
    for (const auto body : {"<logging console='true' console='false'/>", "<logging/><logging/>",
             "<logging name='ignored'/>", "<logging><logger name='oss'/><logger name='oss'/></logging>",
             "<logging><logger name='oss'><ignored/></logger></logging>"})
    {
        DOCTEST_CHECK_THROWS_AS(
            ark::load_application_manifest(files.write(std::string("<xml><plugins/>") + body + "</xml>", "bad.xml")),
            ark::application_manifest_error);
    }
}
