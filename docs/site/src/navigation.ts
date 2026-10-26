import type { BadgeVariant, Breadcrumb, SidebarBadge, SidebarItem } from "@cloudflare/nimbus-docs/types";

type Badge = { en: string; zh: string; variant: BadgeVariant };
type Page = [slug: string, en: string, zh: string, badge?: Badge];
type Group = { en: string; zh: string; children: Entry[]; open?: boolean; badge?: Badge };
type Entry = Page | Group;
const tree: Entry[] = [
  [
    "",
    "Overview",
    "概览"
  ],
  {
    "en": "Get started",
    "zh": "开始使用",
    "open": true,
    "badge": {
      "en": "Start here",
      "zh": "从这里开始",
      "variant": "success"
    },
    "children": [
      [
        "get-started/installation",
        "Install and build",
        "安装与构建"
      ],
      [
        "get-started/quick-start",
        "Quick start",
        "快速开始"
      ],
      [
        "reference/directory-structure",
        "Directory structure",
        "目录结构"
      ]
    ]
  },
  {
    "en": "Concepts",
    "zh": "核心概念",
    "open": true,
    "children": [
      [
        "concepts/architecture",
        "Applications, plugins and modules",
        "应用、插件与模块"
      ],
      [
        "concepts/glossary",
        "Glossary",
        "名词解释"
      ],
      [
        "concepts/application-architecture",
        "Application architecture",
        "应用架构"
      ],
      [
        "concepts/data-driven",
        "Data-driven design",
        "数据驱动"
      ],
      [
        "concepts/server-architecture",
        "Server architecture",
        "服务器架构"
      ]
    ]
  },
  {
    "en": "Guides",
    "zh": "使用指南",
    "children": [
      [
        "guides/modules",
        "Create a module",
        "创建模块"
      ],
      [
        "guides/plugins",
        "Create a plugin",
        "创建插件"
      ],
      [
        "guides/tasks",
        "Coroutine tasks",
        "协程任务"
      ],
      [
        "guides/data-structures",
        "Data structures and metadata",
        "数据结构与元数据"
      ],
      [
        "guides/applications",
        "Application launcher",
        "应用启动器"
      ],
      [
        "guides/examples",
        "Examples by capability",
        "按能力命名的示例"
      ],
      {
        "en": "Service configuration",
        "zh": "服务配置",
        "children": [
          [
            "guides/service-configuration/application-manifests",
            "Application plugin manifests",
            "应用插件清单"
          ]
        ]
      },
      [
        "guides/tools",
        "Configuration tools",
        "配置工具"
      ],
      [
        "networking",
        "Network builds and tests",
        "网络构建与测试"
      ]
    ]
  },
  {
    "en": "Reference",
    "zh": "参考",
    "badge": {
      "en": "API",
      "zh": "API",
      "variant": "info"
    },
    "children": [
      [
        "reference/xxtea",
        "XXTEA helper",
        "XXTEA 工具"
      ],
      [
        "reference/object-vectors",
        "Object vectors",
        "对象指针容器"
      ],
      [
        "reference/kernel",
        "Kernel plugin",
        "Kernel 插件"
      ],
      [
        "reference/network",
        "Network plugin",
        "Net 插件"
      ],
      [
        "reference/http",
        "HTTP server plugin",
        "HTTP 服务端插件"
      ],
      [
        "reference/bus",
        "Bus plugin",
        "Bus 插件"
      ],
      [
        "reference/utility",
        "Utility plugin",
        "Utility 插件"
      ],
      [
        "reference/mysql",
        "MySQL plugin",
        "MySQL 插件"
      ],
      [
        "reference/redis",
        "Redis plugin",
        "Redis 插件"
      ],
      [
        "reference/build-options",
        "Build options",
        "构建选项"
      ],
      [
        "dependencies",
        "Dependencies",
        "依赖管理"
      ],
      [
        "reference/third-party-notices",
        "Third-party notices",
        "第三方许可声明"
      ],
      [
        "reference/protobuf",
        "Protobuf messages",
        "Protobuf 消息与兼容"
      ]
    ]
  },
  {
    "en": "Performance and testing",
    "zh": "性能与测试",
    "children": [
      [
        "performance/benchmark",
        "Benchmarks and capacity",
        "基准测试与容量验收"
      ],
      [
        "performance/receive-buffers",
        "Receive buffers and framing",
        "收包缓冲与拆包"
      ],
      [
        "performance/database-coroutines",
        "Database callbacks and coroutines",
        "数据库回调与协程"
      ],
      [
        "guides/database-validation",
        "Database service acceptance",
        "数据库服务验收"
      ]
    ]
  },
  {
    "en": "Project",
    "zh": "项目",
    "children": [
      [
        "project/faq",
        "FAQ",
        "常见问题"
      ],
      [
        "project/code-style",
        "Code style",
        "代码规范"
      ],
      [
        "project/contributing",
        "Contributing and documentation",
        "贡献与文档开发"
      ],
      [
        "project/changelog",
        "Changelog",
        "更新记录"
      ],
      [
        "project/roadmap",
        "Roadmap",
        "路线图"
      ],
      [
        "project/roadmap-history",
        "Historical roadmap",
        "历史路线图"
      ]
    ]
  },
  {
    "en": "Design proposals",
    "zh": "设计提案",
    "badge": {
      "en": "Draft",
      "zh": "草案",
      "variant": "warning"
    },
    "children": [
      [
        "project/plugin-organization",
        "Plugin organization",
        "插件与仓库组织"
      ],
      [
        "project/data-design",
        "Metadata and nested data",
        "元数据与嵌套结构"
      ],
      [
        "project/data-model-proposal",
        "Data model proposal",
        "数据模型提案"
      ],
      [
        "project/network-design",
        "Cluster and routing design",
        "集群与路由设计"
      ]
    ]
  },
  [
    "project/third-party-notices",
    "Third-party notices",
    "第三方声明"
  ]
];

export function navigation(chinese: boolean, current: string): SidebarItem[] {
  const prefix = chinese ? "/zh/" : "/";
  function badge(value?: Badge): SidebarBadge | undefined {
    return value ? { text: chinese ? value.zh : value.en, variant: value.variant } : undefined;
  }
  function items(entries: Entry[]): SidebarItem[] {
    return entries.map((entry, order): SidebarItem => Array.isArray(entry) ? {
      type: "link", label: entry[chinese ? 2 : 1], href: prefix + entry[0], order,
      badge: badge(entry[3]),
      isCurrent: current.replace(/\/$/, "") === (prefix + entry[0]).replace(/\/$/, ""),
    } : {
      type: "group", label: chinese ? entry.zh : entry.en, order,
      badge: badge(entry.badge),
      collapsed: !entry.open, children: items(entry.children),
    });
  }
  return items(tree);
}

export function pageBreadcrumbs(items: SidebarItem[], title: string, chinese: boolean): Breadcrumb[] {
  function find(entries: SidebarItem[]): Breadcrumb[] | undefined {
    for (const item of entries) {
      if (item.type === "link" && item.isCurrent) return [{ label: title }];
      if (item.type !== "group") continue;
      const path = find(item.children);
      if (path) {
        const overview = item.children.find(child => child.type === "link");
        return [{ label: item.label, href: overview?.type === "link" ? overview.href : undefined }, ...path];
      }
    }
  }
  return [{ label: "ark", href: chinese ? "/zh/" : "/" }, ...(find(items) ?? [{ label: title }])];
}
