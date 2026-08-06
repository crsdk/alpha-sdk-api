---
layout: "default"
title: "Privacy & Data Governance"
description: "Understand how your data is handled when using AI SDK Assistant"
nav_exclude: true
search_exclude: false
---

## Your Data Privacy Matters

AI SDK Assistant is designed with privacy in mind. This page clarifies common questions about data handling and AI data governance when using the tool.

## What Data is Collected?

### Query Data

When you ask questions through your AI assistant (Claude, Claude Code, etc.), AI SDK Assistant receives:

- **Your search queries**: The questions you ask about the Camera Remote SDK
- **Search results**: The SDK documentation, code examples, and API references that match your query
- **SDK context**: Which SDK version and language filters you've set

### What We Don't Collect

AI SDK Assistant does **NOT** collect or store:

- ❌ Your personal information or identity
- ❌ Your conversation history with your AI assistant
- ❌ Your source code or project files
- ❌ Camera credentials or connection details
- ❌ Any proprietary application data

## How is Your Data Used?

### Search Processing

Your queries are processed in real-time to search through the SDK documentation database. The search happens on Railway's cloud infrastructure and results are returned immediately to your AI assistant.

### No Long-Term Storage

AI SDK Assistant does not persistently store your search queries or conversation history. Each search is processed independently without retaining previous searches.

### Analytics (Optional)

Basic usage metrics may be collected to improve the service:

- Number of searches performed
- Popular search topics (aggregated, not individual queries)
- SDK version usage statistics
- Error rates and performance metrics

These analytics are anonymized and used solely for service improvement.

## AI Assistant Data Policies

### Claude (Anthropic)

When using AI SDK Assistant with Claude, Anthropic's data policies apply to your conversation:

- **Claude Desktop/CLI**: Conversations are processed locally and subject to Anthropic's privacy policy
- **Claude Web/Mobile**: Follow Anthropic's terms regarding conversation storage and usage
- Review [Anthropic's Privacy Policy](https://www.anthropic.com/legal/privacy) for details

### Other AI Assistants

Each AI assistant has its own data policies. Check with your specific provider to understand how they handle:

- Conversation data
- Tool/MCP interactions
- Data retention periods
- Opt-out options

## Coding Agent Behavior

Coding agents, like Claude Code and ChatGPT Codex, are specialized LLMs that can independently perform tasks, plan actions, and be given access to external resources. They typically are accessed from your IDE or command line.

These "agentic" AI platforms have built in guardrails to protect against unauthorized access, prompt injection, and unwanted file modifications.

**The AI SDK Assistant does not have control over the agentic capabilities of Claude Code or any other LLM. All existing preferences and guardrails you set for your agents will be respected.**

For example, in Claude Code, you have control over which directories in your filesystem Claude has access to, as well as the permission level. [View the official feature description here.](https://claudelog.com/faqs/--add-dir/)

When prompting Claude, you can control how much autonomy it has when executing tasks. By toggling through the options, you can ensure that Claude waits for approval before taking aciton. For more information on Anthropic's agentic modes, [view the offical site here.](https://claudelog.com/mechanics/plan-mode/)

## MCP (Model Context Protocol)

AI SDK Assistant uses the Model Context Protocol (MCP) to connect with AI assistants. MCP is an open standard that:

- Enables secure communication between AI assistants and external tools
- Processes requests in real-time without intermediary storage
- Allows AI assistants to fetch SDK information on-demand

MCP connections are established directly between your AI assistant and the AI SDK Assistant server without third-party intermediaries.

## Sony SDK Data

### SDK Documentation

AI SDK Assistant hosts and searches through Sony's Camera Remote SDK documentation, including:

- Official SDK documentation and guides
- API references and function signatures
- Code examples from the SDK sample applications
- Compatibility tables and error code references

This content is from Sony's publicly available SDK packages and is used to provide accurate search results.

### No Camera Data

AI SDK Assistant does not interact with your cameras, camera data, or captured images. It only provides documentation and code examples for SDK implementation.

## Data Security

### Encrypted Connections

All connections to AI SDK Assistant use HTTPS encryption to protect data in transit.

### Railway Hosting

AI SDK Assistant is hosted on Railway, a cloud platform with security measures including:

- Infrastructure encryption
- Network isolation
- Regular security updates
- Industry-standard hosting practices

Review [Railway's Security](https://docs.railway.app/reference/security) documentation for more details.

## Your Privacy Rights

### Control Over Queries

You control what questions you ask. AI SDK Assistant only processes queries you explicitly send through your AI assistant.

### Data Access

Since AI SDK Assistant doesn't store individual query histories, there's no personal data to access or delete.

### Service Changes

If data collection or usage policies change, updates will be posted to this documentation.

## Best Practices for Privacy

### 1. Don't Share Sensitive Information

When asking questions, avoid including:

- Camera credentials or passwords
- API keys or authentication tokens
- Proprietary code or trade secrets
- Customer data or personally identifiable information

### 2. Use Generic Examples

Frame questions using generic examples:

- ✅ "How do I connect to a camera using SDK V2?"
- ✅ "Show me error handling for image capture"
- ❌ "Here's my production code with credentials..."

### 3. Review AI Assistant Settings

Check your AI assistant's privacy settings:

- Conversation history retention
- Data usage for model training
- Opt-out options if available

### 4. Local Development

For sensitive development work:

- Use Claude Desktop/CLI for local processing when possible
- Keep proprietary code in your local environment
- Only share necessary context with the AI assistant

## Questions or Concerns?

If you have questions about data privacy or governance:

  - [**Report Issues**](https://sna-ipsa-developer-tools.atlassian.net/jira/software/form/68c76742-1683-4ac8-83f3-a2e2518e5ffc?atlOrigin=eyJpIjoiNTNmNWM0OWYxZTYyNGM2NWIzNTUwOWEzZGRjNmI1ZTIiLCJwIjoiaiJ9) — Submit a bug report or privacy concern
  - [**Contact Support**](mailto:jordan.lee@sony.com) — Email our support team

## Summary

<details markdown="block">
<summary>What data does AI SDK Assistant collect?</summary>

AI SDK Assistant processes your search queries in real-time but does not store individual query histories or personal information. Basic anonymized analytics may be collected for service improvement.

</details>

<details markdown="block">
<summary>Is my code safe?</summary>

Yes. AI SDK Assistant only searches SDK documentation—it doesn't access or store your project code. Don't paste sensitive code into AI conversations as a general security practice.

</details>

<details markdown="block">
<summary>Who can see my questions?</summary>

Your questions are processed by AI SDK Assistant's search service and your chosen AI assistant (Claude, ChatGPT, etc.). Each AI provider has its own privacy policy governing conversation data.

</details>

<details markdown="block">
<summary>How long is data retained?</summary>

AI SDK Assistant doesn't retain individual search queries. Your AI assistant may retain conversation history according to their policies—check with your specific provider.

</details>

<details markdown="block">
<summary>Can I use this for commercial projects?</summary>

Yes. AI SDK Assistant is a free tool for searching Sony's SDK documentation. For commercial SDK usage, review Sony's SDK license agreements.

</details>
