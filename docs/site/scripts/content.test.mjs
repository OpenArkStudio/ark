import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { canonicalFile, convertPage, pageExtension, pageRoute } from "./content.mjs";

test("bilingual routes and home aliases", () => {
  assert.equal(pageRoute("README_CN.md"), "/zh/");
  assert.equal(pageRoute("udp/performance.md"), "/udp/performance");
  assert.equal(pageRoute("THIRD_PARTY_NOTICES_CN.md"), "/zh/third-party-notices");
  assert.equal(pageRoute("features/udp/index_CN.md"), "/zh/features/udp");
  assert.equal(pageRoute("features/index.md"), "/features");
  assert.equal(pageRoute("code-style.md"), "/project/code-style");
  assert.equal(pageRoute("project-layout_CN.md"), "/zh/reference/directory-structure");
  assert.equal(canonicalFile("service-manifests.md"), "guides/service-configuration/application-manifests.md");
  assert.equal(canonicalFile("service-manifests_CN.md"), "guides/service-configuration/application-manifests_CN.md");
  assert.equal(pageRoute("service-manifests.md"), "/guides/service-configuration/application-manifests");
  assert.equal(pageRoute("service-manifests_CN.md"), "/zh/guides/service-configuration/application-manifests");
});

test("relocated manifest sources resolve links from their original Markdown location", () => {
  const files = new Set(["service-manifests.md", "service-manifests_CN.md", "networking.md", "networking_CN.md"]);
  for (const chinese of [false, true]) {
    const suffix = chinese ? "_CN" : "";
    const prefix = chinese ? "/zh" : "";
    const source = `# Manifest\n\n[build](networking${suffix}.md)\n\n[manifest](service-manifests${suffix}.md)\n\n[sample](../config/timers.app.toml)\n\n\`\`\`toml\n[application]\nplugin_config_dir = "conf/"\n\`\`\`\n`;
    const result = convertPage(source, `service-manifests${suffix}.md`, files);
    assert.ok(result.includes(`[build](${prefix}/networking)`));
    assert.ok(result.includes(`[manifest](${prefix}/guides/service-configuration/application-manifests)`));
    assert.ok(result.includes("[sample](https://github.com/OpenArkStudio/ark/blob/main/config/timers.app.toml)"));
    assert.ok(result.includes('```toml\n[application]\nplugin_config_dir = "conf/"\n```'));
  }
});

test("convert parsed links while preserving code and labels", () => {
  const source = '# UDP\n\n[用法](usage_CN.md#示例)\n\n![吞吐](../performance/assets/udp.svg)\n\n```cpp\n// [use](usage_CN.md)\n```\n';
  const result = convertPage(source, "udp/overview_CN.md", new Set(["udp/usage_CN.md"]));
  assert.match(result, /\[用法\]\(\/zh\/udp\/usage#%E7%A4%BA%E4%BE%8B\)/);
  assert.match(result, /!\[吞吐\]\(\/ark\/performance\/assets\/udp.svg\)/);
  assert.match(result, /\/\/ \[use\]\(usage_CN.md\)/);
  assert.match(result, /title: "UDP"/);
  assert.doesNotMatch(result, /# UDP/);
});

test("source references point at GitHub; missing documents fail", () => {
  const result = convertPage("# Page\n\n[source](../include/ark.hpp)", "page.md", new Set());
  assert.match(result, /https:\/\/github.com\/OpenArkStudio\/ark\/blob\/main\/include\/ark.hpp/);
  assert.throws(() => convertPage("# Page\n\n[missing](missing.md)", "page.md", new Set()), /missing document/);
});

test("native Nimbus components use MDX and retain nested Markdown links", () => {
  const source = '# Guide\n\n<Steps>\n\n<Step title="Install">\n\n[Setup](installation.md)\n\n</Step>\n\n</Steps>\n\n<Aside type="caution" title="Warning">\n\nRead the [setup](installation.md).\n\n</Aside>\n';
  const result = convertPage(source, "guide.md", new Set(["installation.md"]));
  assert.equal(pageExtension(source), ".mdx");
  assert.match(result, /import \{ Aside \} from "@\/components\/ui\/aside"/);
  assert.match(result, /import \{ Step, Steps \} from "@\/components\/ui\/steps"/);
  assert.match(result, /\[Setup\]\(\/installation\)/);
  assert.match(result, /\[setup\]\(\/installation\)/);
});

test("component examples inside code blocks and inline code stay plain Markdown", () => {
  const source = '# Authoring\n\nUse `<Steps>`.\n\n```md\n<Aside type="note">\n\nExample\n\n</Aside>\n```\n';
  const result = convertPage(source, "authoring.md", new Set());
  assert.equal(pageExtension(source), ".md");
  assert.doesNotMatch(result, /import \{/);
  assert.ok(result.includes('```md\n<Aside type="note">'));
});

test("routing cards and parallel tabs import vendored component groups", () => {
  const source = `# Guide

<CardGrid>
<LinkCard title="安装 > 构建" href="installation_CN.md#构建" description="Use href='literal.md' as text." />
</CardGrid>

<Tabs syncKey="platform">
<TabItem label="macOS/Linux">

[Install](installation_CN.md)

</TabItem>
<TabItem label="Windows PowerShell">

\`\`\`powershell
# <LinkCard href="unchanged.md" />
\`\`\`

</TabItem>
</Tabs>
`;
  const result = convertPage(source, "get-started/guide_CN.md", new Set(["get-started/installation_CN.md"]));
  assert.equal(pageExtension(source), ".mdx");
  assert.match(result, /import \{ CardGrid \} from "@\/components\/ui\/card-grid"/);
  assert.match(result, /import \{ LinkCard \} from "@\/components\/ui\/link-card"/);
  assert.match(result, /import \{ TabItem, Tabs \} from "@\/components\/ui\/tabs"/);
  assert.ok(result.includes('href="/zh/get-started/installation#%E6%9E%84%E5%BB%BA"'));
  assert.ok(result.includes('description="Use href=\'literal.md\' as text."'));
  assert.ok(result.includes('[Install](/zh/get-started/installation)'));
  assert.ok(result.includes('# <LinkCard href="unchanged.md" />'));
});

test("HTML attributes resolve aliases and assets without editing unrelated attributes", () => {
  const source = `# Paths

<a href='../project-layout_CN.md?view=full#目录'>Layout</a>
<img src='../assets/diagram.svg' alt="diagram" />
<a href="../../src/core/module.hpp">Source</a>
<LinkCard title="Same page" href="#section" data-href="literal.md" />
<a href="https://example.com/guide.md">External</a>
<a href="/guides/modules">Root route</a>

<!-- <LinkCard href="missing.md" /> -->

Use \`<LinkCard href="missing.md" />\`.
`;
  const result = convertPage(source, "guides/paths_CN.md", new Set(["project-layout_CN.md"]));
  assert.ok(result.includes("href='/zh/reference/directory-structure?view=full#%E7%9B%AE%E5%BD%95'"));
  assert.ok(result.includes("src='/ark/assets/diagram.svg'"));
  assert.ok(result.includes('href="https://github.com/OpenArkStudio/ark/blob/main/src/core/module.hpp"'));
  assert.ok(result.includes('href="#section" data-href="literal.md"'));
  assert.ok(result.includes('href="https://example.com/guide.md"'));
  assert.ok(result.includes('href="/guides/modules"'));
  assert.ok(result.includes('<!-- <LinkCard href="missing.md" /> -->'));
  assert.equal(pageExtension('# Plain\n\n<!-- <Tabs> -->\n\n`<Card>`\n\n```mdx\n<LinkCard href="missing.md" />\n```'), ".md");
  assert.throws(() => convertPage('# Missing\n\n<LinkCard href="missing.md" title="Missing" />', "page.md", new Set()), /missing document/);
});

test("other vendored card and disclosure components import their complete families", () => {
  const source = '# Details\n\n<Card title="Summary">\n\nVisible summary.\n\n</Card>\n\n<Collapsible>\n<CollapsibleTrigger>Optional detail</CollapsibleTrigger>\n<CollapsibleContent>\n\nDetail.\n\n</CollapsibleContent>\n</Collapsible>';
  const result = convertPage(source, "details.md", new Set());
  assert.equal(pageExtension(source), ".mdx");
  assert.match(result, /import \{ Card \} from "@\/components\/ui\/card"/);
  assert.match(result, /import \{ Collapsible, CollapsibleTrigger, CollapsibleContent \} from "@\/components\/ui\/collapsible"/);
});

test("module and plugin guides lead with one complete bilingual typed recipe", () => {
  const guides = {};
  const blocks = source => [...source.matchAll(/^```([^\n]*)\n([\s\S]*?)^```/gm)].map(match => ({ language: match[1], code: match[2] }));
  for (const name of ["modules", "plugins"]) {
    const english = readFileSync(new URL(`../../guides/${name}.md`, import.meta.url), "utf8");
    const chinese = readFileSync(new URL(`../../guides/${name}_CN.md`, import.meta.url), "utf8");
    guides[name] = blocks(english);
    assert.deepEqual(blocks(chinese), guides[name], `${name}: bilingual executable recipes diverged`);
    for (const { code } of guides[name].filter(block => block.language === "cpp")) {
      assert.doesNotMatch(code, /ARK_(?:DECLARE_MODULE_FUNCTIONS|DECLARE_PLUGIN|REGISTER_MODULE|UNREGISTER_MODULE)/, `${name}: legacy creation macro returned to the primary recipe`);
    }
  }
  const [api, factory, implementation] = guides.modules.filter(block => block.language === "cpp").map(block => block.code);
  assert.match(api, /class counter_module : public module/);
  assert.doesNotMatch(api, /_impl|module_base/);
  assert.match(factory, /std::unique_ptr<counter_module> make_counter\(\)/);
  assert.match(factory, /std::unique_ptr<sample_module> make_sample\(\)/);
  assert.match(implementation, /class sample_module_impl final : public module_base<sample_module, counter_module>/);
  assert.match(implementation, /std::make_unique<counter_module_impl>/);
  const plugin = guides.plugins.find(block => block.language === "cpp").code;
  assert.match(plugin, /class sample_plugin final : public plugin_base<sample_plugin>/);
  assert.match(plugin, /register_module<counter_module>\(make_counter\(\), module_update::disabled\)/);
  assert.match(plugin, /register_module<sample_module>\(make_sample\(\), module_update::enabled\)/);
  assert.ok(plugin.indexOf("unregister_module<sample_module>()") < plugin.indexOf("unregister_module<counter_module>()"), "remove the consumer before its provider");
  assert.equal((plugin.match(/ARK_EXPORT_FUNC void Dll(?:Entry|Exit)Plugin/g) ?? []).length, 2);
  assert.match(guides.plugins.find(block => block.language === "cmake").code, /add_library\(sample_plugin SHARED sample_plugin.cpp sample_module.cpp\)/);
});
