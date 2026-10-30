import assert from "node:assert/strict";
import { readFile, readdir, stat } from "node:fs/promises";
import { join, resolve } from "node:path";
import { parse } from "parse5";
import { unified } from "unified";
import remarkParse from "remark-parse";
import { pageRoute } from "./content.mjs";

const root = resolve("dist");
const pages = [];
async function collect(directory) {
  for (const item of await readdir(directory, { withFileTypes: true })) {
    const file = join(directory, item.name);
    if (item.isDirectory()) await collect(file);
    else if (item.name.endsWith(".html")) pages.push(file);
  }
}
await collect(root);
const references = new Set();
const documents = new Map();
const failures = [];
const routes = JSON.parse(await readFile(new URL("../src/legacy-routes.json", import.meta.url), "utf8"));
const redirects = new Set(Object.keys(routes).map(route => join(root, route, "index.html")));
const navigation = new Set();
for (const page of pages) {
  const document = parse(await readFile(page, "utf8"));
  const ids = new Set();
  let headings = 0;
  function walk(node) {
    if (node.tagName === "h1") headings++;
    if (node.tagName === "a" && node.attrs?.some(attr => attr.name === "data-nb-sidebar-link")) {
      const href = node.attrs.find(attr => attr.name === "href")?.value;
      if (href) navigation.add(href.replace(/\/$/, ""));
    }
    for (const { name, value } of node.attrs ?? []) {
      if (name === "id") ids.add(value);
      if ((name === "href" || name === "src" || (name === "value" && node.tagName === "option")) && value.startsWith("/") && !value.startsWith("//")) {
        if (value !== "/ark" && !value.startsWith("/ark/")) failures.push(`${page}: reference escapes project base: ${value}`);
        else references.add(value);
      }
    }
    for (const child of node.childNodes ?? []) walk(child);
  }
  walk(document);
  if (!page.endsWith("404.html")) assert.equal(headings, 1, `${page}: one page heading required`);
  documents.set(page, ids);
}
for (const [page] of documents) {
  if (page.endsWith("404.html") || redirects.has(page)) continue;
  const route = "/ark/" + page.slice(root.length + 1).replace(/(?:^|\/)index\.html$/, "").replace(/\/$/, "");
  if (!navigation.has(route.replace(/\/$/, ""))) failures.push(`${page}: missing navigation entry`);
}
for (const [from, redirect] of Object.entries(routes)) {
  const target = join(root, redirect.to, "index.html");
  if (!documents.has(target)) failures.push(`${from}: missing redirect destination ${redirect.to}`);
  for (const [anchor, destination] of Object.entries(redirect.anchors)) {
    if (!documents.get(join(root, destination, "index.html"))?.has(anchor)) failures.push(`${from}#${anchor}: missing redirected anchor in ${destination}`);
  }
}
for (const reference of references) {
  const url = new URL(reference, "https://openarkstudio.github.io");
  let target = join(root, decodeURIComponent(url.pathname.slice("/ark".length)));
  try {
    if ((await stat(target)).isDirectory()) target = join(target, "index.html");
    await stat(target);
    const anchor = decodeURIComponent(url.hash.slice(1));
    if (anchor && documents.has(target) && !documents.get(target).has(anchor)) failures.push(`${reference}: missing anchor`);
  } catch {
    failures.push(`${reference}: missing static file`);
  }
}
assert.deepEqual(failures, [], "Broken site references:\n" + failures.join("\n"));
for (const file of ["llms.txt", "llms-full.txt", "zh/llms.txt", "networking/index.md", "zh/networking/index.md", "sitemap-index.xml", "robots.txt"]) {
  assert.ok((await stat(join(root, file))).size > 0, `missing AI/SEO output: ${file}`);
}
for (const prefix of ["", "zh/"]) {
  const home = await readFile(join(root, prefix, "index.html"), "utf8");
  assert.match(home, /data-home-links/);
  assert.match(home, /data-language-switch/);
  const install = await readFile(join(root, prefix, "get-started/installation/index.html"), "utf8");
  assert.match(install, /data-steps/);
  assert.match(install, /data-step/);
  const markdown = await readFile(join(root, prefix, "get-started/installation/index.md"), "utf8");
  assert.match(markdown, /\n1\. /);
  assert.doesNotMatch(markdown, /<(?:Aside|Steps|Step)[\s>]/);
  const plugin = await readFile(join(root, prefix, "guides/plugins/index.html"), "utf8");
  assert.match(plugin, /role="note"/);
  assert.match(plugin, /class="astro-code/);
  const manifestSlug = "guides/service-configuration/application-manifests";
  const manifestRoute = "/ark/" + prefix + manifestSlug;
  const manifest = parse(await readFile(join(root, prefix, manifestSlug, "index.html"), "utf8"));
  const groups = new Set();
  const labels = new Set();
  const languages = new Set();
  const text = node => node.value ?? (node.childNodes ?? []).map(text).join("");
  function inspect(node) {
    const attrs = new Map((node.attrs ?? []).map(attr => [attr.name, attr.value]));
    if (attrs.has("data-nb-sidebar-group-label")) groups.add(text(node).trim());
    if (attrs.has("data-nb-sidebar-link") && attrs.get("href")?.replace(/\/$/, "") === manifestRoute) labels.add(text(node).trim());
    if (node.tagName === "option") languages.add(attrs.get("value")?.replace(/\/$/, ""));
    for (const child of node.childNodes ?? []) inspect(child);
  }
  inspect(manifest);
  assert.ok(groups.has(prefix ? "服务配置" : "Service configuration"), "missing localized service configuration group");
  assert.ok(labels.has(prefix ? "应用插件清单" : "Application plugin manifests"), "missing localized manifest navigation");
  assert.ok(languages.has("/ark/" + manifestSlug), "missing English manifest language target");
  assert.ok(languages.has("/ark/zh/" + manifestSlug), "missing Chinese manifest language target");
  const manifestMarkdown = await readFile(join(root, prefix, manifestSlug, "index.md"), "utf8");
  assert.match(manifestMarkdown, /```toml\n\[application\]/);
  assert.match(manifestMarkdown, /load_application_manifest/);
  assert.ok(manifestMarkdown.includes(`](/ark/${prefix}networking)`), "manifest Markdown lost its build-guide link");
  const codeStylePath = join(root, prefix, "project/code-style/index.html");
  const codeStyle = parse(await readFile(codeStylePath, "utf8"));
  let codeStyleArticle;
  const codeStyleLanguages = new Set();
  function inspectCodeStyle(node) {
    if (node.tagName === "article") codeStyleArticle = node;
    if (node.tagName === "option") {
      codeStyleLanguages.add(node.attrs?.find(attr => attr.name === "value")?.value?.replace(/\/$/, ""));
    }
    for (const child of node.childNodes ?? []) inspectCodeStyle(child);
  }
  inspectCodeStyle(codeStyle);
  assert.ok(codeStyleArticle, "missing canonical code-style article");
  const codeStyleText = text(codeStyleArticle);
  const codeStyleMarkdown = await readFile(join(root, prefix, "project/code-style/index.md"), "utf8");
  // The skipped aliases still contain accepted compatibility notes. Check their
  // API literals in actual output so adding a migration only to an alias fails.
  const styleAlias = await readFile(new URL(`../../code-style${prefix ? "_CN" : ""}.md`, import.meta.url), "utf8");
  const migrationStart = styleAlias.indexOf(prefix ? "## C++20 兼容层迁移" : "## C++20 compatibility migration");
  assert.ok(migrationStart >= 0, "missing accepted code-style migration boundary");
  const migrationTokens = [...styleAlias.slice(migrationStart).matchAll(/`([^`\n]+)`/g)].map(match => match[1]);
  for (const token of new Set([...migrationTokens, "CMAKE_EXPORT_COMPILE_COMMANDS=ON", "src/application/main.cpp",
    "meta_define.hpp", "session_counter", "RAII"])) {
    assert.ok(codeStyleText.includes(token), `${prefix}canonical code-style HTML lost ${token}`);
    assert.ok(codeStyleMarkdown.includes(token), `${prefix}canonical code-style Markdown lost ${token}`);
  }
  for (const anchor of prefix ? ["c20-兼容层迁移", "标准工具迁移"] : ["c20-compatibility-migration", "standard-utility-migration"]) {
    assert.ok(documents.get(codeStylePath).has(anchor), `${prefix}canonical code-style lost #${anchor}`);
  }
  for (const language of ["/ark/project/code-style", "/ark/zh/project/code-style"]) {
    assert.ok(codeStyleLanguages.has(language), `canonical code-style lost language target ${language}`);
  }

}
// Exercise publication, not just the source converter: Nimbus adds the project
// base and serializes cards, every tab panel and nested steps independently.
const markdownParser = unified().use(remarkParse);
const componentPages = ["README", "get-started/installation", "get-started/quick-start",
  "guides/applications", "guides/modules", "guides/plugins",
  "concepts/architecture", "concepts/application-architecture"];
function descendants(node, childKey = "children") {
  return [node, ...(node[childKey] ?? []).flatMap(child => descendants(child, childKey))];
}
const nodeText = node => node.value ?? (node.childNodes ?? []).map(nodeText).join("");
for (const prefix of ["", "zh/"]) {
  for (const slug of componentPages) {
    const file = `${slug}${prefix ? "_CN" : ""}.md`;
    const source = await readFile(new URL(`../../${file}`, import.meta.url), "utf8");
    const route = pageRoute(file).replace(/^\//, "");
    const output = join(root, route);
    const html = parse(await readFile(join(output, "index.html"), "utf8"));
    const markdown = await readFile(join(output, "index.md"), "utf8");
    const sourceNodes = descendants(markdownParser.parse(source));
    const publishedNodes = descendants(markdownParser.parse(markdown));
    const htmlNodes = descendants(html, "childNodes");
    const code = nodes => nodes.filter(node => node.type === "code").map(node => node.value);
    assert.deepEqual(code(publishedNodes), code(sourceNodes), `${file}: AI Markdown lost or changed a code example`);
    const htmlCode = htmlNodes.filter(node => node.tagName === "pre").map(node => nodeText(node).trimEnd());
    assert.deepEqual(htmlCode, code(sourceNodes).map(value => value.trimEnd()), `${file}: HTML lost or changed a code example`);
    const htmlText = nodeText(html);
    for (const node of sourceNodes.filter(node => node.type === "html")) {
      for (const tag of node.value.matchAll(/<(?:LinkCard|TabItem|Aside|Step)(?=[\s/>])[^>]*>/g)) {
        for (const label of tag[0].matchAll(/(?:title|description|label)="([^"]+)"/g)) {
          const panelLabel = htmlNodes.some(node => node.attrs?.some(attr => attr.name === "data-nb-tab-label" && attr.value === label[1]));
          assert.ok(htmlText.includes(label[1]) || panelLabel, `${file}: HTML lost component label ${label[1]}`);
          assert.ok(markdown.includes(label[1]), `${file}: AI Markdown lost component label ${label[1]}`);
        }
        if (tag[0].startsWith("<LinkCard ")) {
          const href = tag[0].match(/href="([^"]+)"/)?.[1];
          assert.ok(href, `${file}: routing card needs a link`);
          const target = new URL(href, `https://source.invalid/docs/${file}`);
          const expected = "/ark" + pageRoute(target.pathname.slice("/docs/".length)) + target.search + target.hash;
          assert.ok(htmlNodes.some(node => node.tagName === "a" && node.attrs?.some(attr => attr.name === "href" && attr.value === expected)), `${file}: HTML lost card link ${expected}`);
          assert.ok(markdown.includes(`](${expected})`), `${file}: AI Markdown lost card link ${expected}`);
        }
      }
    }
    assert.doesNotMatch(markdown, /<(?:CardGrid|LinkCard|Tabs|TabItem|Steps|Step|Aside)[\s/>]/, `${file}: raw component leaked into AI Markdown`);
    assert.doesNotMatch(markdown, /\]\(\/ark\/ark\//, `${file}: AI Markdown doubled the project base`);
    if (slug === "README") {
      const grids = htmlNodes.filter(node => node.attrs?.some(attr => attr.name === "data-home-links"));
      assert.equal(grids.length, 1, `${file}: duplicate home routing grid`);
      assert.ok(documents.get(join(output, "index.html")).has("where-to-go-next"), `${file}: home routing anchor changed`);
      for (const target of ["get-started/installation", "concepts/architecture", "guides/plugins", "networking"]) {
        assert.ok(markdown.includes(`](/ark/${prefix}${target})`), `${file}: home card lost its base-aware Markdown link`);
      }
    }
    if (slug.startsWith("get-started/")) {
      for (const label of ["macOS/Linux", "Windows PowerShell"]) {
        assert.ok(htmlNodes.some(node => node.attrs?.some(attr => attr.name === "data-nb-tab-label" && attr.value === label)), `${file}: missing ${label} tab`);
        assert.ok(markdown.includes(`### ${label}`), `${file}: Markdown lost ${label} tab label`);
      }
    }
  }
}
console.log(`Checked ${pages.length} static HTML pages and ${references.size} internal references, language switching, cards, callouts, highlighting and AI Markdown; verified examples and component labels on ${componentPages.length * 2} bilingual guide outputs.`);

// Check emitted reference/report content within the existing documentation gate.
{
// Additive publication check; run after npm run check --prefix docs/site.
// Optional --base=<revision> also protects the reviewed source/data boundary.
const { default: assert } = await import("node:assert/strict");
const { execFileSync } = await import("node:child_process");
const { readFile, readdir } = await import("node:fs/promises");
const { fileURLToPath } = await import("node:url");
const { dirname, join, posix } = await import("node:path");
const { parse } = await import("parse5");
const { unified } = await import("unified");
const { default: remarkParse } = await import("remark-parse");
const { getHeadingsFromHtml } = await import("@cloudflare/nimbus-docs/runtime");
const { canonicalFile, pageRoute } = await import("./content.mjs");

const site = fileURLToPath(new URL("../", import.meta.url));
const docs = dirname(site);
const repo = dirname(docs);
const base = process.argv.find(arg => arg.startsWith("--base="))?.slice(7);
// Source-derived corrections approved during the reference copy audit. These
// are scoped to stale inventory/path prose, never measurement data or examples.
const pluginCmake = await readFile(join(repo, "src/plugins/CMakeLists.txt"), "utf8");
const netInventory = pluginCmake.match(/set\(ark_net_public_headers([\s\S]*?)\n\)/)?.[1];
assert.ok(netInventory, "missing explicit Net header inventory");
const netHeaderCount = [...netInventory.matchAll(/\/net\/[^"\n]+/g)].length;
assert.ok((await readFile(join(repo, "src/core/CMakeLists.txt"), "utf8")).includes('"bus.hpp"'), "Bus must remain in the core public inventory");
await readFile(join(repo, "src/core/bus.hpp"));
const sourceCorrections = {
  "reference/build-options.md": [["Net's 18 public headers", `Net's ${netHeaderCount} public headers`]],
  "reference/build-options_CN.md": [["Net 的 18 个公开头", `Net 的 ${netHeaderCount} 个公开头`]],
  "reference/directory-structure.md": [["all 18 Net public headers", `all ${netHeaderCount} Net public headers`]],
  "reference/directory-structure_CN.md": [["分别编译 18 个 Net 公开头", `分别编译 ${netHeaderCount} 个 Net 公开头`]],
  "reference/bus.md": [["`<ark/bus.hpp>` remain in `include/ark/`", "`<ark/bus.hpp>` live in `src/core/bus.hpp`"]],
  "reference/bus_CN.md": [["共享地址和配置类型 `<ark/bus.hpp>` 仍位于 `include/ark/`", "共享地址和配置类型 `<ark/bus.hpp>` 源码位于 `src/core/bus.hpp`"]],
};
const parser = unified().use(remarkParse);
const walk = (node, key = "children") => [node, ...(node[key] ?? []).flatMap(child => walk(child, key))];
const text = (node, key = "children") => node.value ?? (node[key] ?? []).map(child => text(child, key)).join("");
const sourceNodes = source => walk(parser.parse(source));
const codes = source => sourceNodes(source).filter(node => node.type === "code").map(node => node.value);
const inline = source => sourceNodes(source).filter(node => node.type === "inlineCode").map(node => node.value);
const headings = source => sourceNodes(source).filter(node => node.type === "heading");
const rows = source => source.split("\n").filter(line => /^\s*\|/.test(line));
const numbers = source => new Set(source.match(/\d+(?:[.,]\d+)*/g) ?? []);
const documents = [];
for (const section of ["reference", "performance"]) {
  for (const file of await readdir(join(docs, section))) {
    if (!file.endsWith(".md") || file.startsWith("third-party-notices")) continue;
    const relative = section + "/" + file;
    if (canonicalFile(relative) === relative) documents.push(relative);
  }
}
let examples = 0;
let anchors = 0;
let dataRows = 0;
for (const file of documents) {
  const source = await readFile(join(docs, file), "utf8");
  for (const [, corrected] of sourceCorrections[file] ?? []) assert.ok(source.includes(corrected), `${file}: stale source inventory/path fact`);
  const output = join(site, "dist", pageRoute(file).replace(/^\//, ""));
  const markdown = await readFile(join(output, "index.md"), "utf8");
  const htmlSource = await readFile(join(output, "index.html"), "utf8");
  const html = parse(htmlSource);
  const htmlNodes = walk(html, "childNodes");
  const visible = text(html, "childNodes");
  assert.deepEqual(codes(markdown), codes(source), `${file}: exported examples differ`);
  assert.deepEqual(htmlNodes.filter(node => node.tagName === "pre").map(node => text(node, "childNodes").trimEnd()), codes(source).map(code => code.trimEnd()), `${file}: HTML examples differ`);
  examples += codes(source).length;
  for (const token of new Set(inline(source))) {
    assert.ok(markdown.includes(token), `${file}: Markdown lost API/default ${token}`);
    assert.ok(visible.includes(token), `${file}: HTML lost API/default ${token}`);
  }
  const ids = new Set(htmlNodes.flatMap(node => (node.attrs ?? []).filter(attr => attr.name === "id").map(attr => attr.value)));
  let previous = base ? execFileSync("git", ["show", `${base}:docs/${file}`], { cwd: repo, encoding: "utf8" }) : source;
  for (const [stale, corrected] of sourceCorrections[file] ?? []) previous = previous.replace(stale, corrected);
  const publishedHeadings = getHeadingsFromHtml(htmlSource);
  const currentHeadings = headings(source);
  for (const heading of headings(previous).filter(node => node.depth > 1)) {
    const label = text(heading);
    assert.ok(currentHeadings.some(node => node.depth === heading.depth && text(node) === label), `${file}: source heading removed: ${label}`);
    const index = publishedHeadings.findIndex(node => node.depth === heading.depth && node.text === label);
    assert.ok(index >= 0, `${file}: published heading removed: ${label}`);
    const [{ slug }] = publishedHeadings.splice(index, 1);
    assert.ok(ids.has(slug), `${file}: missing heading anchor #${slug}`);
    anchors++;
  }
  for (const node of sourceNodes(source).filter(node => node.type === "image")) {
    if (/^[a-z]+:|^\//i.test(node.url)) continue;
    const asset = posix.normalize(posix.join(dirname(file), node.url));
    const expected = "/ark/" + asset;
    assert.ok(markdown.includes(expected), `${file}: Markdown lost chart ${expected}`);
    assert.ok(htmlNodes.some(node => node.tagName === "img" && node.attrs?.some(attr => attr.name === "src" && attr.value === expected)), `${file}: HTML lost chart ${expected}`);
    if (base) {
      assert.equal(await readFile(join(docs, asset), "utf8"), execFileSync("git", ["show", `${base}:docs/${asset}`], { cwd: repo, encoding: "utf8" }), `${file}: measured chart changed`);
    }
  }
  if (base) {
    assert.deepEqual(codes(source), codes(previous), `${file}: source examples changed`);
    for (const token of new Set(inline(previous))) assert.ok(inline(source).includes(token), `${file}: removed API/default ${token}`);
    for (const number of numbers(previous)) assert.ok(numbers(source).has(number), `${file}: lost recorded number ${number}`);
    if (file.startsWith("performance/")) {
      for (const row of rows(previous)) assert.ok(rows(source).includes(row), `${file}: measured/environment row changed`);
      dataRows += rows(previous).length;
    }
  }
}
console.log(`Verified ${documents.length} canonical reference/performance HTML+Markdown pairs, ${examples} examples, ${anchors} anchors${base ? ` and ${dataRows} preserved measurement/environment rows against ${base}` : ""}.`);

}

for (const prefix of ["", "zh/"]) {
  const route = prefix + "project/lifecycle-closeout/";
  const html = await readFile(join(root, route, "index.html"), "utf8");
  const markdown = await readFile(join(root, route, "index.md"), "utf8");
  const source = await readFile(new URL(`../../project/lifecycle-closeout${prefix ? "_CN" : ""}.md`, import.meta.url), "utf8");
  assert.match(html, /data-steps/, `${route}: shutdown procedure did not render`);
  for (const step of source.matchAll(/<Step title="([^"]+)">/g)) {
    assert.ok(markdown.includes(`**${step[1]}**`), `${route}: lost exported shutdown step`);
  }
  for (const token of ["stopping_acceptance", "draining", "timed_out", "cleanup_timed_out", "failed", "close_and_wait()", "reg_external_root_callback()", "manager.update()", "task_group::poll()", "17", "12", "--shutdown_timeout_ms"]) {
    assert.ok(markdown.includes(token), `${route}: missing lifecycle constraint ${token}`);
  }
  for (const target of ["guides/applications", "project/startup-failure-cleanup"]) {
    assert.ok(markdown.includes(`](/ark/${prefix}${target})`), `${route}: missing procedure card destination`);
  }
  assert.doesNotMatch(markdown, /<(?:CardGrid|LinkCard|Aside|Steps|Step)[\s/>]/, `${route}: raw component in Markdown`);
}
console.log("Checked bilingual lifecycle procedures and retention constraints.");
