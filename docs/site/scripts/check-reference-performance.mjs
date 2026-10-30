// Additive publication check; run after npm run check --prefix docs/site.
// Optional --base=<revision> also protects the reviewed source/data boundary.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFile, readdir } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, join, posix } from "node:path";
import { parse } from "parse5";
import { unified } from "unified";
import remarkParse from "remark-parse";
import GithubSlugger from "github-slugger";
import { canonicalFile, pageRoute } from "./content.mjs";

const site = fileURLToPath(new URL("../", import.meta.url));
const docs = dirname(site);
const repo = dirname(docs);
const base = process.argv.find(arg => arg.startsWith("--base="))?.slice(7);
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
  const output = join(site, "dist", pageRoute(file).replace(/^\//, ""));
  const markdown = await readFile(join(output, "index.md"), "utf8");
  const html = parse(await readFile(join(output, "index.html"), "utf8"));
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
  const previous = base ? execFileSync("git", ["show", `${base}:docs/${file}`], { cwd: repo, encoding: "utf8" }) : source;
  const slugger = new GithubSlugger();
  for (const heading of headings(previous).filter(node => node.depth > 1)) {
    const id = slugger.slug(text(heading));
    assert.ok(ids.has(id), `${file}: lost #${id}`);
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
