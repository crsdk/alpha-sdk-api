---
layout: "default"
title: "Compatibility"
description: "Supported SDK versions, programming languages, and AI platforms"
nav_exclude: true
search_exclude: false
---

## Supported SDK Versions

The AI SDK Assistant supports the following SDK versions:

| Version      | Status    | Index                 | Type                                   | Notes                                      |
| ------------ | --------- | --------------------- | -------------------------------------- | ------------------------------------------ |
| **V2.01.00** | Latest    | `sdk-rag-system-v2-1` | Camera Remote only (C++)               | Current default for all new sessions       |
| **V2.00.00** | Supported | `sdk-rag-system-v2`   | Camera Remote (C++, C#), PTP-2/3 (C++) | C# examples only available in this version |

{: .note }
> V1.14.00 is no longer indexed. If you need legacy V1 documentation, contact support.


### Programming Languages

| Language | SDK Versions       | Platforms             |
| -------- | ------------------ | --------------------- |
| **C++**  | V2.01.00, V2.00.00 | Windows, macOS, Linux |
| **C#**   | V2.00.00 only      | Windows               |
| **Bash** | V2.01.00, V2.00.00 | macOS, Linux          |

### Database Statistics

The AI SDK Assistant indexes **13,333+** pieces of SDK content across documentation, API references, code examples, and compatibility tables.

---

## Compatible AI Platforms

The AI SDK Assistant works with any AI platform that supports MCP (Model Context Protocol). **We only guarantee support for Anthropic's Claude products.**

| Platform                        | Account Required                   | Tool Permissions           | Code Generation                  | Best For                 |
| ------------------------------- | ---------------------------------- | -------------------------- | -------------------------------- | ------------------------ |
| **Claude (Web/Mobile/Desktop)** | Pro, Max, or Enterprise (\$20+/mo) | Granular UI controls       | Good — no codebase access        | Most users               |
| **Claude Code (CLI)**           | Paid Claude account                | File-based config          | Excellent — full codebase access | Developers, automation   |
| **Cursor**                      | Free or Pro                        | Settings UI or JSON config | Excellent — inline code editing  | IDE-integrated workflows |

### Platform Details

<details markdown="block">
<summary>Claude (Web, Mobile, Desktop) — Recommended</summary>

**Best for**: Most users who want the easiest setup with visual controls.

- **Granular tool control**: Enable/disable individual tools through an intuitive UI
- **Permission levels**: Set each tool to "always ask" or "allow unsupervised"
- **Multi-platform**: Works on web, iOS/Android, and desktop
- **Best MCP support**: Built by Anthropic (creators of MCP)
- **200K token context window**

Requires Claude Pro (\$20/month), Max, or Enterprise account.

{: .note }
> Review [Anthropic's Privacy Policy](https://www.anthropic.com/legal/privacy) for data handling details.


</details>

<details markdown="block">
<summary>Claude Code (CLI) — Recommended for Developers</summary>

**Best for**: Developers who want SDK assistance integrated into their development workflow.

- **Full codebase access**: Claude Code can read and edit your project files directly
- **Granular permissions**: Approve tools via settings files (version control friendly)
- **Automation-friendly**: Scriptable, supports subagents for parallel SDK research
- **Multiple scopes**: User, project, or local MCP server configurations
- **200K token context window**

Requires a paid Claude account and the Claude Code CLI.

{: .note }
> Claude Code can execute commands on your local machine. Review tool permissions carefully and only approve actions you're comfortable with.


</details>

<details markdown="block">
<summary>Cursor — Recommended for IDE Users</summary>

**Best for**: Developers who prefer an IDE-integrated experience with inline code editing.

- **Native MCP support**: Add servers via Settings UI or `.cursor/mcp.json`
- **Inline code generation**: AI can edit files directly in your editor
- **Project and global scopes**: Per-project or machine-wide configuration
- **Supports stdio, SSE, and streamable-http transports**

Available with Cursor Free or Pro accounts.

</details>


### Which Platform Should You Choose?

- **Claude (Web/Desktop)** — easiest setup, best for learning the SDK and exploring documentation
- **Claude Code** — best for active development, codebase-aware, supports subagents and automation
- **Cursor** — best if you already use Cursor as your IDE and want SDK assistance inline

{: .warning }
> We do not recommend using ChatGPT with the AI SDK Assistant. OpenAI has limited MCP support, leading to unreliable tool usage and poor code generation. Only use ChatGPT if you already have a subscription and cannot access Claude.


## Getting Started

- [**Setup Guide**]({{ site.baseurl }}/mcp-server/setup) — Step-by-step instructions for connecting AI SDK Assistant to your chosen platform
