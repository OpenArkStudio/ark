import { unified } from "unified";
import remarkParse from "remark-parse";
import { pageLocations, sourceAliases } from "./migration-source.mjs";

const parser = unified().use(remarkParse);
const componentGroups = {
  aside: ["Aside"],
  steps: ["Step", "Steps"],
  card: ["Card"],
  "card-grid": ["CardGrid"],
  "link-card": ["LinkCard"],
  tabs: ["TabItem", "Tabs"],
  collapsible: ["Collapsible", "CollapsibleTrigger", "CollapsibleContent"],
};
const componentPattern = new RegExp(`<(${Object.values(componentGroups).flat().join("|")})(?=[\\s/>])`, "g");

export function canonicalFile(file) {
  return pageLocations[file] ?? sourceAliases[file] ?? file;
}

export function pageRoute(file) {
  file = canonicalFile(file);
  const chinese = file.endsWith("_CN.md");
  let slug = file.replace(/(?:_CN)?\.md$/, "");
  if (slug === "README") slug = "";
  if (slug === "THIRD_PARTY_NOTICES") slug = "third-party-notices";
  slug = slug.replace(/\/index$/, "");
  return (chinese ? "/zh/" : "/") + slug;
}

function text(node) {
  return node.value ?? (node.children ?? []).map(text).join("");
}

function components(tree) {
  const names = new Set();
  function walk(node) {
    if (node.type === "html" && !node.value.startsWith("<!--")) {
      for (const match of node.value.matchAll(componentPattern)) names.add(match[1]);
    }
    for (const child of node.children ?? []) walk(child);
  }
  walk(tree);
  return names;
}

export function pageExtension(source) {
  return components(parser.parse(source)).size ? ".mdx" : ".md";
}

export function convertPage(source, file, files) {
  const tree = parser.parse(source);
  const heading = tree.children.find((node) => node.type === "heading" && node.depth === 1);
  if (!heading) throw new Error(`${file}: missing page heading`);
  const title = text(heading);
  const replacements = [{ start: heading.position.start.offset, end: heading.position.end.offset, value: "" }];
  function destinationFor(value, image = false) {
    if (/^(?:[a-z][a-z\d+.-]*:|\/\/|#)/i.test(value)) return value;
    if (value.startsWith("/")) return value;
    const url = new URL(value, `https://source.invalid/docs/${file}`);
    const pathname = decodeURIComponent(url.pathname);
    let destination;
    if (!pathname.startsWith("/docs/")) {
      destination = "https://github.com/OpenArkStudio/ark/blob/main" + pathname;
    } else {
      const target = pathname.slice(6);
      if (target.endsWith(".md")) {
        if (!files.has(target)) throw new Error(`${file}: missing document ${target}`);
        destination = pageRoute(target);
      } else {
        // Nimbus prefixes Markdown and HTML/MDX hrefs, but not image sources.
        destination = (image ? "/ark/" : "/") + target;
      }
    }
    return destination + url.search + url.hash;
  }
  function walk(node) {
    if ((node.type === "link" || node.type === "image" || node.type === "definition") && node.url) {
      const destination = destinationFor(node.url, node.type === "image");
      const start = node.position.start.offset;
      const original = source.slice(start, node.position.end.offset);
      // Locate only the parsed destination; code blocks and labels stay byte-identical.
      const offset = original.lastIndexOf(node.url);
      if (offset < 0) throw new Error(`${file}: unsupported escaped link ${node.url}`);
      replacements.push({ start: start + offset, end: start + offset + node.url.length, value: destination });
    }
    if (node.type === "html" && !node.value.startsWith("<!--")) {
      // Only quoted attributes inside parsed tags: never code, labels or prose.
      for (const tag of node.value.matchAll(/<[A-Za-z][\w:.-]*(?:[^<>"']|"[^"]*"|'[^']*')*>/g)) {
        for (const attr of tag[0].matchAll(/([\w:-]+)\s*=\s*(["'])(.*?)\2/g)) {
          if (attr[1] !== "href" && attr[1] !== "src") continue;
          const start = node.position.start.offset + tag.index + attr.index + attr[0].indexOf(attr[2]) + 1;
          replacements.push({ start, end: start + attr[3].length, value: destinationFor(attr[3], attr[1] === "src") });
        }
      }
    }
    for (const child of node.children ?? []) walk(child);
  }
  walk(tree);
  let body = source;
  for (const edit of replacements.sort((a, b) => b.start - a.start)) {
    body = body.slice(0, edit.start) + edit.value + body.slice(edit.end);
  }
  const names = components(tree);
  const imports = [];
  for (const [group, components] of Object.entries(componentGroups)) {
    const used = components.filter(name => names.has(name));
    if (used.length) imports.push(`import { ${used.join(", ")} } from "@/components/ui/${group}";`);
  }
  return `---\ntitle: ${JSON.stringify(title)}\n---\n\n${imports.length ? imports.join("\n") + "\n\n" : ""}${body.trim()}\n`;
}
