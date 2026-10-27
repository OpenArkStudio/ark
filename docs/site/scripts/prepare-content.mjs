import { existsSync as fsExists } from "node:fs";
import { readdir, readFile, mkdir, writeFile, cp, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { posix, join } from "node:path";
import { canonicalFile, convertPage, pageExtension } from "./content.mjs";
import { sourceAliases } from "./migration-source.mjs";

const site = fileURLToPath(new URL("../", import.meta.url));
const docs = fileURLToPath(new URL("../../", import.meta.url));
async function pages(directory, prefix = "") {
  const result = [];
  for (const item of await readdir(directory, { withFileTypes: true })) {
    if (item.name.startsWith(".") || item.name === "site" || item.name === "licenses") continue;
    const relative = posix.join(prefix, item.name);
    if (item.isDirectory()) result.push(...await pages(join(directory, item.name), relative));
    else if (item.name.endsWith(".md") && item.name !== "_sidebar.md") result.push(relative);
  }
  return result;
}
const files = new Set(await pages(docs));
for (const [alias, target] of Object.entries(sourceAliases)) {
  if (files.has(alias) && !files.has(target)) throw new Error(`${alias}: missing alias destination ${target}`);
}
const content = join(site, "src/content");
await rm(content, { recursive: true, force: true });
let prepared = 0;
for (const file of files) {
  if (Object.hasOwn(sourceAliases, file)) continue;
  const chinese = file.endsWith("_CN.md");
  const sibling = chinese ? file.replace(/_CN\.md$/, ".md") : file.replace(/\.md$/, "_CN.md");
  if (!files.has(sibling)) throw new Error(`${file}: missing translation`);
  const slug = canonicalFile(file).replace(/(?:_CN)?\.md$/, "").replace(/^README$/, "index");
  const source = await readFile(join(docs, file), "utf8");
  const target = join(content, chinese ? "zh" : "docs", slug + pageExtension(source));
  await mkdir(join(target, ".."), { recursive: true });
  await writeFile(target, convertPage(source, file, files));
  prepared++;
}
await cp(join(docs, "assets"), join(site, "public/assets"), { recursive: true });
await mkdir(join(site, "public/performance"), { recursive: true });
if (fsExists(join(docs, "performance/assets"))) await cp(join(docs, "performance/assets"), join(site, "public/performance/assets"), { recursive: true });
for (const name of (fsExists(join(docs, "performance")) ? await readdir(join(docs, "performance")) : [])) {
  if (name.endsWith(".json")) await cp(join(docs, "performance", name), join(site, "public/performance", name));
}
console.log(`Prepared ${prepared} bilingual Markdown pages.`);
