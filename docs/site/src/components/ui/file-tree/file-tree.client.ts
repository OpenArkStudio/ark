import { mount } from "@cloudflare/nimbus-docs/client";

function cloneIcon(tpl: HTMLTemplateElement | null): Node {
  return tpl ? tpl.content.cloneNode(true) : document.createTextNode("");
}

function createSpacer(): HTMLElement {
  const span = document.createElement("span");
  span.className = "ft-sp";
  span.setAttribute("aria-hidden", "true");
  return span;
}

function createEmptyIcon(): HTMLElement {
  const span = document.createElement("span");
  span.className = "ft-icon";
  span.setAttribute("aria-hidden", "true");
  return span;
}

function escapeHtml(value: string): string {
  return value
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function getSafeLabel(nodes: ChildNode[], stripTrailingSlash = false): string {
  return nodes
    .map((node) => {
      let text = node.textContent ?? "";
      if (stripTrailingSlash) {
        text = text.replace(/\/\s*$/, "");
      }
      text = text.trim();
      if (!text) return "";

      if (node.nodeType === Node.ELEMENT_NODE && (node as Element).tagName === "STRONG") {
        return `<strong>${escapeHtml(text)}</strong>`;
      }

      return escapeHtml(text);
    })
    .join("");
}

function createLabel(label: string): HTMLSpanElement {
  const span = document.createElement("span");
  span.className = "ft-label";
  span.innerHTML = label;
  return span;
}

function createEntry(nodes: ChildNode[], stripTrailingSlash = false): HTMLSpanElement {
  const text = nodes.map((node) => node.textContent ?? "").join("");
  const separator = /\s+—\s+/.exec(text);
  if (!separator) return createLabel(getSafeLabel(nodes, stripTrailingSlash));

  const nameNodes: ChildNode[] = [];
  let remaining = separator.index;
  for (const node of nodes) {
    const value = node.textContent ?? "";
    const name = value.slice(0, Math.max(0, remaining));
    if (name) {
      if (node.nodeType === Node.ELEMENT_NODE && (node as Element).tagName === "STRONG") {
        const strong = document.createElement("strong");
        strong.textContent = name;
        nameNodes.push(strong);
      } else {
        nameNodes.push(document.createTextNode(name));
      }
    }
    remaining -= value.length;
    if (remaining <= 0) break;
  }

  const content = document.createElement("span");
  content.className = "ft-content";
  content.appendChild(createLabel(getSafeLabel(nameNodes, stripTrailingSlash)));
  const description = document.createElement("span");
  description.className = "ft-description";
  description.textContent = " — " + text.slice(separator.index + separator[0].length).trim();
  content.appendChild(description);
  return content;
}

mount("[data-file-tree]", (tree) => {
  const chevTpl = tree.querySelector<HTMLTemplateElement>("[data-ft-icon-chev]");
  const folderTpl = tree.querySelector<HTMLTemplateElement>("[data-ft-icon-folder]");
  const fileTpl = tree.querySelector<HTMLTemplateElement>("[data-ft-icon-file]");

  for (const li of tree.querySelectorAll("li")) {
    const childUl = li.querySelector<HTMLUListElement>(":scope > ul");
    const text = li.textContent?.trim() ?? "";

    if (text === "..." || text === "…") {
      li.setAttribute("data-placeholder", "");
      li.replaceChildren(createSpacer(), createEmptyIcon(), createLabel("⋯"));
      continue;
    }

    if (childUl) {
      const labelNodes = [...li.childNodes].filter((node) => node !== childUl);

      const details = document.createElement("details");
      details.open = true;
      const summary = document.createElement("summary");
      summary.appendChild(cloneIcon(chevTpl));
      summary.appendChild(cloneIcon(folderTpl));
      summary.appendChild(createEntry(labelNodes, true));
      details.appendChild(summary);
      details.appendChild(childUl);
      li.replaceChildren(details);
    } else {
      li.setAttribute("data-file", "");
      li.replaceChildren(createSpacer(), cloneIcon(fileTpl), createEntry([...li.childNodes]));
    }
  }

  // Init only transforms the DOM once (native <details> handles expand/collapse);
  // it registers no listeners, so there is nothing to tear down.
  return () => {};
});
