// =============================================================================
// MCP Server Installation — Configure coding agent MCP servers
// =============================================================================

import { existsSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { homedir } from 'node:os';
import { execSync } from 'node:child_process';
import { colors, symbols, stagger } from './theme.js';

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const CAMERA_HELP_SERVER = {
  name: 'Camera Help Guides',
  desc: 'AI agents can read Sony Alpha / Cinema / PTZ help guides and manuals to debug physical camera configuration',
  key: 'CameraHelp',
  url: 'https://camera-rag-agent-production.up.railway.app/mcp',
} as const;

export const MCP_SERVERS = [CAMERA_HELP_SERVER] as const;

export type AgentName = 'claude-code' | 'vscode' | 'cursor';
export type ScopeName = 'user' | 'project';

export const AGENTS: { name: AgentName; desc: string }[] = [
  { name: 'claude-code', desc: 'Claude Code CLI' },
  { name: 'vscode', desc: 'VS Code / Copilot' },
  { name: 'cursor', desc: 'Cursor AI' },
];

export const SCOPES: { name: ScopeName; desc: string }[] = [
  { name: 'user', desc: 'Global (home directory)' },
  { name: 'project', desc: 'Project (current directory)' },
];

// ---------------------------------------------------------------------------
// Config paths & formats
// ---------------------------------------------------------------------------

function getConfigPath(agent: AgentName, scope: ScopeName): string {
  const home = homedir();
  const cwd = process.cwd();

  switch (agent) {
    case 'claude-code':
      return scope === 'user'
        ? join(home, '.claude.json')
        : join(cwd, '.mcp.json');
    case 'vscode':
      return scope === 'user'
        ? join(home, '.vscode', 'mcp.json')
        : join(cwd, '.vscode', 'mcp.json');
    case 'cursor':
      return scope === 'user'
        ? join(home, '.cursor', 'mcp.json')
        : join(cwd, '.cursor', 'mcp.json');
  }
}

function getServersKey(agent: AgentName): 'mcpServers' | 'servers' {
  return agent === 'vscode' ? 'servers' : 'mcpServers';
}

function buildServerEntry(url: string, agent: AgentName): Record<string, unknown> {
  const isWindows = process.platform === 'win32';
  const entry: Record<string, unknown> = {};

  // Claude Code and VS Code include "type": "stdio"; Cursor does not
  if (agent !== 'cursor') {
    entry.type = 'stdio';
  }

  if (isWindows) {
    entry.command = 'cmd';
    entry.args = ['/c', 'npx', 'mcp-remote', url];
  } else {
    entry.command = 'npx';
    entry.args = ['mcp-remote', url];
  }

  return entry;
}

function readConfig(path: string): Record<string, any> {
  if (!existsSync(path)) return {};
  try {
    const raw = readFileSync(path, 'utf-8');
    return JSON.parse(raw);
  } catch {
    return {};
  }
}

function writeConfig(path: string, config: Record<string, any>): void {
  const dir = dirname(path);
  if (!existsSync(dir)) {
    mkdirSync(dir, { recursive: true });
  }
  writeFileSync(path, JSON.stringify(config, null, 2) + '\n');
}

function getDisplayPath(path: string): string {
  const home = homedir();
  if (path.startsWith(home)) {
    return '~' + path.slice(home.length);
  }
  return path;
}

// ---------------------------------------------------------------------------
// Health check
// ---------------------------------------------------------------------------

async function httpPing(url: string, timeout = 5000): Promise<boolean> {
  try {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout);
    const response = await fetch(url, {
      method: 'HEAD',
      signal: controller.signal,
    });
    clearTimeout(timer);
    return response.ok || response.status === 405; // HEAD may not be allowed
  } catch {
    return false;
  }
}

function checkClaudeCodeConnection(serverKey: string): boolean {
  try {
    // Suppress stderr via stdio (portable) instead of a POSIX `2>/dev/null`
    // redirection, which cmd.exe does not understand on Windows.
    const output = execSync('claude mcp list', {
      encoding: 'utf-8',
      timeout: 10000,
      stdio: ['ignore', 'pipe', 'ignore'],
    });
    return output.includes(serverKey) && output.includes('Connected');
  } catch {
    return false;
  }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

export async function mcpStatus(): Promise<void> {
  const dim = colors.dim;
  const text = colors.text;
  const accent = colors.accent;

  console.log('\n  MCP Server\n');
  console.log(`    ${text(CAMERA_HELP_SERVER.name)}`);
  console.log(`    ${dim(CAMERA_HELP_SERVER.desc)}\n`);

  console.log('  Installed configs:');

  // Check each agent/scope combo
  const checks: { label: string; agent: AgentName; scope: ScopeName }[] = [
    { label: 'Claude Code (user)', agent: 'claude-code', scope: 'user' },
    { label: 'Claude Code (project)', agent: 'claude-code', scope: 'project' },
    { label: 'VS Code (user)', agent: 'vscode', scope: 'user' },
    { label: 'VS Code (project)', agent: 'vscode', scope: 'project' },
    { label: 'Cursor (user)', agent: 'cursor', scope: 'user' },
    { label: 'Cursor (project)', agent: 'cursor', scope: 'project' },
  ];

  for (const check of checks) {
    const configPath = getConfigPath(check.agent, check.scope);
    const config = readConfig(configPath);
    const key = getServersKey(check.agent);
    const servers = config[key] || {};
    const installed = CAMERA_HELP_SERVER.key in servers;
    const label = dim(check.label.padEnd(26));

    if (installed) {
      console.log(`    ${label}${symbols.installed} ${text(CAMERA_HELP_SERVER.key)}`);
    } else {
      console.log(`    ${label}${symbols.skipped} ${dim('not configured')}`);
    }
  }

  console.log(`\n  Run ${accent('mcp install <agent> <scope>')} to add the server.\n`);
}

export async function mcpInstall(agent?: string, scope?: string): Promise<void> {
  const dim = colors.dim;
  const text = colors.text;
  const accent = colors.accent;

  // Validate agent
  if (!agent || !['claude-code', 'vscode', 'cursor'].includes(agent)) {
    console.log(`\n  Usage: mcp install <agent> <scope>`);
    console.log(`  Agents: claude-code, vscode, cursor`);
    console.log(`  Scopes: user, project\n`);
    console.log(`  Installs the ${accent(CAMERA_HELP_SERVER.name)} MCP server.`);
    console.log(`  ${dim(CAMERA_HELP_SERVER.desc)}\n`);
    return;
  }

  // Validate scope
  if (!scope || !['user', 'project'].includes(scope)) {
    console.log(`\n  Usage: mcp install ${agent} <scope>`);
    console.log(`  Scopes: user, project\n`);
    return;
  }

  const agentName = agent as AgentName;
  const scopeName = scope as ScopeName;
  const configPath = getConfigPath(agentName, scopeName);
  const serversKey = getServersKey(agentName);
  const displayPath = getDisplayPath(configPath);
  const platformLabel = process.platform === 'win32' ? 'Windows (cmd /c npx)' :
                        process.platform === 'darwin' ? 'macOS (npx)' : 'Linux (npx)';

  const agentLabel = AGENTS.find(a => a.name === agentName)?.desc ?? agent;

  // Check if already configured
  const existingConfig = readConfig(configPath);
  const existingServers = existingConfig[serversKey] || {};

  if (CAMERA_HELP_SERVER.key in existingServers) {
    console.log(`\n  ${symbols.existing} ${CAMERA_HELP_SERVER.name} already configured for ${agentLabel} (${scopeName}).\n`);
    console.log(`    ${dim('Config:')} ${displayPath}`);
    console.log(`    ${symbols.installed} ${text(CAMERA_HELP_SERVER.name)}`);
    console.log(`\n  Run ${accent('mcp status')} for details.\n`);
    return;
  }

  console.log(`\n  Installing ${CAMERA_HELP_SERVER.name}...\n`);
  await stagger(100);
  console.log(`    ${dim('Agent:'.padEnd(12))}${text(agentLabel)}`);
  console.log(`    ${dim('Scope:'.padEnd(12))}${text(scopeName)} ${dim(`(${displayPath})`)}`);
  console.log(`    ${dim('Platform:'.padEnd(12))}${text(platformLabel)}`);
  console.log('');

  // Read existing config and merge
  const config = readConfig(configPath);
  if (!config[serversKey]) {
    config[serversKey] = {};
  }

  // For VS Code, ensure "inputs" array exists
  if (agentName === 'vscode' && !config.inputs) {
    config.inputs = [];
  }

  await stagger(60);
  config[serversKey][CAMERA_HELP_SERVER.key] = buildServerEntry(CAMERA_HELP_SERVER.url, agentName);

  const name = text(CAMERA_HELP_SERVER.name.padEnd(26));
  const host = CAMERA_HELP_SERVER.url.replace('https://', '').replace('/mcp', '');
  console.log(`    ${symbols.added} ${name}${symbols.arrow} ${dim(host)}`);

  // Write config
  writeConfig(configPath, config);

  // Health check
  console.log(`\n    ${dim('Health check...')}`);
  await stagger(200);

  const reachable = await httpPing(CAMERA_HELP_SERVER.url);
  await stagger(60);
  if (reachable) {
    console.log(`    ${symbols.connected} ${name}${colors.green('connected')}`);
  } else {
    console.log(`    ${symbols.unreachable} ${name}${colors.red('unreachable')}`);
  }

  // Agent-side connection check (best-effort, claude-code only)
  if (agentName === 'claude-code') {
    const agentConnected = checkClaudeCodeConnection(CAMERA_HELP_SERVER.key);
    if (agentConnected) {
      console.log(`    ${symbols.connected} ${dim('Claude Code reports the server is connected.')}`);
    }
  }

  console.log(`\n    ${symbols.added} ${accent('1')} server configured.`);

  if (agentName === 'claude-code') {
    console.log(`    Restart Claude Code to activate.\n`);
  } else {
    console.log(`    Restart ${agentLabel} to activate.\n`);
  }
}

// ---------------------------------------------------------------------------
// Entry point (called from camera-server.ts)
// ---------------------------------------------------------------------------

export async function mcpCommand(subcommand?: string, arg2?: string, arg3?: string): Promise<void> {
  if (!subcommand || subcommand === 'status') {
    await mcpStatus();
    return;
  }

  if (subcommand === 'install') {
    await mcpInstall(arg2, arg3);
    return;
  }

  console.log(`\n  Unknown subcommand: ${subcommand}`);
  console.log(`  Usage: mcp [status | install <agent> <scope>]\n`);
}

// ---------------------------------------------------------------------------
// Status line helper (used by terminal.ts welcome header)
// ---------------------------------------------------------------------------

export function getMcpStatusSummary(): { configured: number; total: number } {
  const agents: AgentName[] = ['claude-code', 'vscode', 'cursor'];
  const scopes: ScopeName[] = ['user', 'project'];

  for (const agent of agents) {
    for (const scope of scopes) {
      const configPath = getConfigPath(agent, scope);
      const config = readConfig(configPath);
      const key = getServersKey(agent);
      const servers = config[key] || {};
      if (CAMERA_HELP_SERVER.key in servers) {
        return { configured: 1, total: 1 };
      }
    }
  }

  return { configured: 0, total: 1 };
}
