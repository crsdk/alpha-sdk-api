// =============================================================================
// Terminal UI — Full-screen canvas with fixed input bar
// =============================================================================

import chalk from 'chalk';
import { colors } from './theme.js';
import { getMcpStatusSummary } from './mcp.js';

const COMMANDS = [
  { name: 'doctor', desc: 'Check toolchain, SDK, and build state' },
  { name: 'build', desc: 'Compile the native REST server', hasArgs: true },
  { name: 'start', desc: 'Run the built server (background)', hasArgs: true },
  { name: 'status', desc: 'Check whether the server is running' },
  { name: 'stop', desc: 'Stop a running server' },
  { name: 'upgrade', desc: 'git pull the repo, then rebuild' },
  { name: 'versions', desc: 'List active + archived SDK versions' },
  { name: 'use', desc: 'Swap an archived SDK into shared/', hasArgs: true },
  { name: 'install', desc: 'Extract the Sony SDK from a zip', hasArgs: true },
  { name: 'mcp', desc: 'Docs MCP server management', hasArgs: true },
  { name: 'gen:mcp', desc: 'Regenerate MCP flat tools from the spec' },
  { name: 'build:mcp', desc: 'Compile the MCP server → mcp/dist' },
  { name: 'pack:mcp', desc: 'Build alpha-camera.mcpb' },
  { name: 'help', desc: 'Show available commands' },
  { name: 'version', desc: 'Print the CLI version' },
];

const MCP_SUBCOMMANDS = [
  { name: 'install', desc: 'Configure MCP servers' },
  { name: 'status', desc: 'Check server status' },
];

const MCP_AGENT_OPTIONS = [
  { name: 'claude-code', desc: 'Claude Code CLI' },
  { name: 'vscode', desc: 'VS Code / Copilot' },
  { name: 'cursor', desc: 'Cursor AI' },
];

const MCP_SCOPE_OPTIONS = [
  { name: 'user', desc: 'Global (home directory)' },
  { name: 'project', desc: 'Project (current directory)' },
];

const PORT_OPTIONS = [
  { name: '--port', desc: 'Server port (default 8080)' },
];

const BUILD_OPTIONS = [
  { name: '--clean', desc: 'Wipe the build dir first' },
];

interface PopupItem {
  name: string;
  desc: string;
}

// Strip ANSI escape codes to get visible character count
function stripAnsi(str: string): string {
  return str.replace(/\x1b\[[0-9;]*m/g, '');
}

// Truncate string to N visible characters, preserving ANSI codes
function truncateVisible(str: string, maxVisible: number): string {
  let visible = 0;
  let i = 0;
  while (i < str.length && visible < maxVisible) {
    // Skip ANSI escape sequences
    if (str[i] === '\x1b' && str[i + 1] === '[') {
      const end = str.indexOf('m', i);
      if (end !== -1) {
        i = end + 1;
        continue;
      }
    }
    visible++;
    i++;
  }
  // Include any trailing ANSI reset sequences
  while (i < str.length && str[i] === '\x1b') {
    const end = str.indexOf('m', i);
    if (end !== -1) {
      i = end + 1;
    } else {
      break;
    }
  }
  return str.slice(0, i);
}

export interface SystemInfo {
  version: string;
  platform: string;
  platformName: string;
  nodeVersion: string;
  inRepo: boolean;
  sdkInstalled: boolean;
  serverBuilt: boolean;
  binaryPath?: string;
}

export class TerminalApp {
  private outputLines: string[] = [];
  private userInputLines: Set<number> = new Set(); // indices of user-submitted lines
  private inputBuffer = '';
  private showPopup = false;
  private popupIndex = 0;
  private scrollOffset = 0; // 0 = pinned to bottom
  private running = false;
  private loading = false;
  private spinnerFrame = 0;
  private spinnerTimer: ReturnType<typeof setInterval> | null = null;
  private spinnerLineIdx = -1; // index in outputLines where spinner is rendered
  private onSubmit: ((input: string) => Promise<void>) | null = null;
  private directWrite: typeof process.stdout.write | null = null; // bypass stdout capture

  // Owl blink animation
  private static SPINNER = [
    '(◉◉)',
    '(◉◉)',
    '(◉◉)',
    '(◉◉)',
    '(◔◔)',
    '(──)',
    '(◔◔)',
    '(◉◉)',
  ];

  constructor(private systemInfo: SystemInfo) {}

  start(onSubmit: (input: string) => Promise<void>): void {
    this.onSubmit = onSubmit;
    this.running = true;

    this.buildWelcomeHeader();

    // Enter alternate screen buffer
    process.stdout.write('\x1b[?1049h');
    process.stdout.write('\x1b[?25h');

    process.stdin.setRawMode(true);
    process.stdin.resume();
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', this.handleKey);

    process.stdout.on('resize', this.render);

    this.render();
  }

  private buildWelcomeHeader(): void {
    const si = this.systemInfo;
    const check = colors.green('✓');
    const cross = colors.red('✗');
    const dim = colors.dim;
    const accent = colors.accent;
    const text = colors.text;

    this.outputLines.push('');
    this.outputLines.push(`${accent('◆')} ${chalk.hex('#fafaf9').bold('crsdk')} ${dim('— Alpha Camera REST API developer CLI')}`);
    this.outputLines.push('');
    this.outputLines.push(`${dim('Version'.padEnd(14))}${text(`crsdk@${si.version}`)}`);
    this.outputLines.push(`${dim('Platform'.padEnd(14))}${text(si.platformName)} ${dim(`(${si.platform})`)}`);
    this.outputLines.push(`${dim('Node.js'.padEnd(14))}${text(si.nodeVersion)}`);

    if (!si.inRepo) {
      this.outputLines.push(`${dim('Repo'.padEnd(14))}${cross} ${text('not inside a clone of alpha-sdk-api')}`);
    } else {
      if (si.sdkInstalled) {
        this.outputLines.push(`${dim('SDK'.padEnd(14))}${check} ${text('placed in shared/')}`);
      } else {
        this.outputLines.push(`${dim('SDK'.padEnd(14))}${cross} ${text('not placed')} ${dim('— run')} ${accent('install --zip')}`);
      }
      if (si.serverBuilt) {
        this.outputLines.push(`${dim('Server'.padEnd(14))}${check} ${text('built')}`);
        if (si.binaryPath) {
          this.outputLines.push(`${dim('Binary'.padEnd(14))}${dim(si.binaryPath)}`);
        }
      } else {
        this.outputLines.push(`${dim('Server'.padEnd(14))}${cross} ${text('not built')} ${dim('— run')} ${accent('build')}`);
      }
    }

    const mcpStatus = getMcpStatusSummary();
    if (mcpStatus.configured > 0) {
      this.outputLines.push(`${dim('MCP'.padEnd(14))}${check} ${text(`${mcpStatus.configured} docs server${mcpStatus.configured > 1 ? 's' : ''} configured`)} ${dim('— run mcp status')}`);
    } else {
      this.outputLines.push(`${dim('MCP'.padEnd(14))}${dim('not configured')} ${dim('— run mcp install')}`);
    }

    this.outputLines.push('');
    this.outputLines.push(`${dim('First run:')} ${accent('install --zip <sony-sdk.zip>')} ${dim('→')} ${accent('build')} ${dim('→')} ${accent('start')}`);
    this.outputLines.push('');
    this.outputLines.push(`${dim('Type a command or')} ${accent('/')} ${dim('to browse. Press')} ${text('esc')} ${dim('to exit.')}`);
    this.outputLines.push('');
  }

  private startSpinner(): void {
    this.loading = true;
    this.spinnerFrame = 0;
    // Add spinner line to canvas
    this.spinnerLineIdx = this.outputLines.length;
    this.outputLines.push(this.buildSpinnerLine());
    this.scrollOffset = 0;
    this.render();
    this.spinnerTimer = setInterval(() => {
      this.spinnerFrame = (this.spinnerFrame + 1) % TerminalApp.SPINNER.length;
      if (this.spinnerLineIdx >= 0) {
        this.outputLines[this.spinnerLineIdx] = this.buildSpinnerLine();
      }
      this.render();
    }, 200);
  }

  private stopSpinner(): void {
    this.loading = false;
    if (this.spinnerTimer) {
      clearInterval(this.spinnerTimer);
      this.spinnerTimer = null;
    }
    // Remove spinner line from canvas
    if (this.spinnerLineIdx >= 0 && this.spinnerLineIdx < this.outputLines.length) {
      this.outputLines.splice(this.spinnerLineIdx, 1);
    }
    this.spinnerLineIdx = -1;
  }

  private buildSpinnerLine(): string {
    const frame = colors.accent(TerminalApp.SPINNER[this.spinnerFrame]);
    return `${frame}${colors.dim(' running...')}`;
  }

  stop(): void {
    this.running = false;
    this.stopSpinner();
    process.stdin.removeListener('data', this.handleKey);
    process.stdout.removeListener('resize', this.render);
    process.stdin.setRawMode(false);
    process.stdin.pause();
    process.stdout.write('\x1b[?1049l');
  }

  addOutput(text: string): void {
    const lines = text.split('\n');
    this.outputLines.push(...lines);
    this.scrollOffset = 0; // snap to bottom on new output
    if (this.running) this.render();
  }

  captureOutput(fn: () => Promise<void>): Promise<void> {
    return new Promise(async (resolve) => {
      const origWrite = process.stdout.write.bind(process.stdout);
      let captured = '';

      // Save direct write so render can bypass the interceptor
      this.directWrite = origWrite;

      process.stdout.write = ((chunk: any): boolean => {
        if (typeof chunk === 'string') {
          captured += chunk;
        } else {
          captured += chunk.toString();
        }
        return true;
      }) as any;

      try {
        await fn();
      } finally {
        process.stdout.write = origWrite;
        this.directWrite = null;
        this.stopSpinner();
      }

      if (captured) {
        const lines = captured.split('\n');
        if (lines.length > 0 && lines[lines.length - 1] === '') {
          lines.pop();
        }
        this.outputLines.push(...lines);
      }

      this.scrollOffset = 0; // snap to bottom
      this.render();
      // Safety re-render after event loop drains (handles async command completion)
      setTimeout(() => { this.render(); resolve(); }, 0);
    });
  }

  private popupMode: 'commands' | 'args' = 'commands';
  private popupDepth = 0; // tracks how deep we are in the completion chain

  private getPopupItems(): PopupItem[] {
    const raw = this.inputBuffer.startsWith('/')
      ? this.inputBuffer.slice(1)
      : this.inputBuffer;
    const trimmed = raw.trimStart();
    const parts = trimmed.split(/\s+/).filter(p => p.length > 0);
    const hasTrailingSpace = trimmed.endsWith(' ');
    // argCount = number of complete tokens after the command
    // "mcp" → 0, "mcp " → 0 (trailing space = ready for arg 1), "mcp install" → 1, "mcp install " → 1
    const argCount = hasTrailingSpace ? parts.length : parts.length - 1;

    // Level 1+: command typed, check for args
    if (argCount >= 1) {
      const cmd = parts[0].toLowerCase();
      const isComplete = COMMANDS.some(c => c.name === cmd);
      // Command is fully typed but has no args — nothing more to complete
      if (isComplete && !COMMANDS.find(c => c.name === cmd && c.hasArgs)) {
        return [];
      }
      const argCmd = COMMANDS.find(c => c.name === cmd && c.hasArgs);
      if (argCmd) {
        this.popupMode = 'args';

        // MCP multi-level: mcp → subcommand → agent → scope
        if (cmd === 'mcp') {
          const sub = parts[1]?.toLowerCase() || '';

          // Level 1: mcp subcommands
          if (argCount === 1) {
            this.popupDepth = 1;
            if (!sub || hasTrailingSpace) return MCP_SUBCOMMANDS;
            return MCP_SUBCOMMANDS.filter(s => s.name.startsWith(sub));
          }

          // Only 'install' has further args
          if (sub === 'install') {
            const agentArg = parts[2]?.toLowerCase() || '';

            // Level 2: agent selection
            if (argCount === 2) {
              this.popupDepth = 2;
              if (!agentArg || hasTrailingSpace) return MCP_AGENT_OPTIONS;
              return MCP_AGENT_OPTIONS.filter(a => a.name.startsWith(agentArg));
            }

            // Level 3: scope selection (final — server is fixed to CameraHelp)
            if (argCount === 3) {
              this.popupDepth = 3;
              const scopeArg = parts[3]?.toLowerCase() || '';
              if (!scopeArg || hasTrailingSpace) return MCP_SCOPE_OPTIONS;
              return MCP_SCOPE_OPTIONS.filter(s => s.name.startsWith(scopeArg));
            }
          }

          // No more completions after scope
          return [];
        }

        // start: --port option
        if (cmd === 'start') {
          if (argCount === 1) {
            this.popupDepth = 1;
            const sub = parts[1] || '';
            if (!sub || hasTrailingSpace) return PORT_OPTIONS;
            return PORT_OPTIONS.filter(s => s.name.startsWith(sub));
          }
          return [];
        }

        // build: --clean option
        if (cmd === 'build') {
          if (argCount === 1) {
            this.popupDepth = 1;
            const sub = parts[1] || '';
            if (!sub || hasTrailingSpace) return BUILD_OPTIONS;
            return BUILD_OPTIONS.filter(s => s.name.startsWith(sub));
          }
          return [];
        }

        // install (--zip <path>) and use (<archive name>) take free-form args
        // with no fixed completion set — nothing to suggest.
        return [];
      }
    }

    // Level 0: show commands
    this.popupMode = 'commands';
    this.popupDepth = 0;
    const query = parts[0]?.toLowerCase() || '';
    if (!query) return COMMANDS;
    return COMMANDS.filter(c => c.name.startsWith(query));
  }

  private getContentHeight(): number {
    const rows = process.stdout.rows || 24;
    const popupList = this.showPopup ? this.getPopupItems() : [];
    const popupHeight = this.showPopup ? Math.min(popupList.length + 2, 10) : 0;
    return rows - 4 - popupHeight; // top border + input area (3 rows) + popup
  }

  render = (): void => {
    if (!this.running) return;

    const rows = process.stdout.rows || 24;
    const cols = process.stdout.columns || 80;
    const LEFT = 2; // consistent left margin

    // Clear screen
    this.write('\x1b[2J\x1b[H');

    // Top border — full width
    const labelText = 'camera-sdk';
    const topFill = Math.max(0, cols - labelText.length - 4);
    const topBorder = colors.border('── ') + colors.accent(labelText) + colors.border(' ' + '─'.repeat(topFill));
    this.moveTo(1, 1);
    this.write(topBorder);

    // Content area
    const popupList = this.showPopup ? this.getPopupItems() : [];
    const popupHeight = this.showPopup ? Math.min(popupList.length + 2, 10) : 0;
    const contentHeight = this.getContentHeight();

    // Calculate visible window with scroll
    const totalLines = this.outputLines.length;
    const maxScroll = Math.max(0, totalLines - contentHeight);
    const scrollPos = maxScroll - this.scrollOffset;
    const startIdx = Math.max(0, scrollPos);
    const visible = this.outputLines.slice(startIdx, startIdx + contentHeight);

    const userInputBg = chalk.bgHex('#0a1620'); // very subtle blue tint

    for (let i = 0; i < visible.length; i++) {
      this.moveTo(2 + i, 1);
      const globalIdx = startIdx + i;
      const isUserLine = this.userInputLines.has(globalIdx);
      const line = ' '.repeat(LEFT) + visible[i];
      if (isUserLine) {
        // Fill entire row with tinted background
        const padded = line + ' '.repeat(Math.max(0, cols - stripAnsi(line).length));
        this.write(userInputBg(padded));
      } else {
        this.write(truncateVisible(line, cols));
      }
    }

    // Scroll indicator
    if (totalLines > contentHeight) {
      const canScrollUp = startIdx > 0;
      const canScrollDown = this.scrollOffset > 0;
      if (canScrollUp) {
        this.moveTo(2, cols - 1);
        this.write(colors.dim('↑'));
      }
      if (canScrollDown) {
        this.moveTo(1 + contentHeight, cols - 1);
        this.write(colors.dim('↓'));
      }
    }

    // Command popup
    if (this.showPopup && popupList.length > 0) {
      const boxW = Math.min(48, cols - LEFT - 4);
      const popupTop = rows - 3 - popupHeight;

      this.moveTo(popupTop, LEFT + 1);
      this.write(colors.border('┌' + '─'.repeat(boxW) + '┐'));

      for (let i = 0; i < popupList.length; i++) {
        this.moveTo(popupTop + 1 + i, LEFT + 1);
        const cmd = popupList[i];
        const selected = i === this.popupIndex;
        const prefix = selected ? colors.accent('│ › ') : colors.border('│   ');
        const name = selected ? colors.accent(cmd.name.padEnd(14)) : colors.text(cmd.name.padEnd(14));
        const desc = colors.dim(cmd.desc);
        this.write(prefix + name + desc);
      }

      this.moveTo(popupTop + 1 + popupList.length, LEFT + 1);
      this.write(colors.border('└' + '─'.repeat(boxW) + '┘'));
    }

    // Input area
    this.renderInputBar();
    this.write('\x1b[?25h');
  };

  // Write to screen, bypassing stdout capture when active
  private write(data: string): void {
    if (this.directWrite) {
      this.directWrite(data);
    } else {
      process.stdout.write(data);
    }
  }

  private renderInputBar(): void {
    const rows = process.stdout.rows || 24;
    const cols = process.stdout.columns || 80;
    const LEFT = 2;
    const inputBorder = colors.border('─'.repeat(cols));

    this.moveTo(rows - 2, 1);
    this.write(inputBorder);

    this.moveTo(rows - 1, 1);

    const promptVisualLen = LEFT + 2; // margin + "› "

    const hintText = 'esc to exit';
    const gap = Math.max(1, cols - promptVisualLen - this.inputBuffer.length - hintText.length);
    this.write(
      ' '.repeat(LEFT) +
      colors.accent('› ') +
      this.inputBuffer +
      ' '.repeat(gap) +
      colors.dim(hintText),
    );

    this.moveTo(rows, 1);
    this.write(inputBorder);

    // Position cursor at input prompt
    this.moveTo(rows - 1, promptVisualLen + this.inputBuffer.length + 1);
  }

  private moveTo(row: number, col: number): void {
    this.write(`\x1b[${row};${col}H`);
  }

  private handleKey = (key: string): void => {
    if (key === '\x1b') {
      if (this.showPopup) {
        this.showPopup = false;
        this.popupIndex = 0;
        this.render();
      } else {
        this.stop();
        process.exit(0);
      }
      return;
    }

    if (key === '\x03') {
      this.stop();
      process.exit(0);
    }

    if (key === '\r' || key === '\n') {
      if (this.showPopup) {
        const filtered = this.getPopupItems();
        if (filtered.length > 0) {
          this.applyCompletion(filtered[this.popupIndex].name);
          // Check if there's a next completion level
          this.inputBuffer += ' ';
          const next = this.getPopupItems();
          if (next.length > 0) {
            // Chain into next level — don't submit yet
            this.showPopup = true;
            this.popupIndex = 0;
            this.render();
            return;
          }
          // No more levels — trim trailing space and fall through to submit
          this.inputBuffer = this.inputBuffer.trimEnd();
        }
        this.showPopup = false;
        this.popupIndex = 0;
      } else {
        // No popup open — check if the typed command has args and should show a popup
        const raw = this.inputBuffer.trimStart();
        const parts = raw.split(/\s+/);
        const cmd = parts[0]?.toLowerCase();
        const argCmd = COMMANDS.find(c => c.name === cmd && c.hasArgs);
        if (argCmd && !raw.includes(' ')) {
          // Command typed without args — open popup to guide selection
          this.inputBuffer = cmd + ' ';
          this.showPopup = true;
          this.popupIndex = 0;
          this.render();
          return;
        }
      }

      const input = this.inputBuffer.trim();
      this.inputBuffer = '';

      if (input) {
        this.userInputLines.add(this.outputLines.length);
        this.outputLines.push(`${colors.accent('›')} ${colors.text(input)}`);
        this.outputLines.push('');
        this.scrollOffset = 0;

        // Show spinner in canvas immediately
        this.startSpinner();

        if (this.onSubmit) {
          this.onSubmit(input).then(() => {
            this.outputLines.push('');
            this.scrollOffset = 0;
            this.render();
          });
        }
      } else {
        this.render();
      }
      return;
    }

    if (key === '\x7f' || key === '\b') {
      if (this.inputBuffer.length > 0) {
        this.inputBuffer = this.inputBuffer.slice(0, -1);
        this.updatePopup();
        this.render();
      }
      return;
    }

    // Arrow keys
    if (key === '\x1b[A') {
      if (this.showPopup) {
        if (this.popupIndex > 0) {
          this.popupIndex--;
          this.render();
        }
      } else {
        // Scroll up
        const maxScroll = Math.max(0, this.outputLines.length - this.getContentHeight());
        if (this.scrollOffset < maxScroll) {
          this.scrollOffset++;
          this.render();
        }
      }
      return;
    }
    if (key === '\x1b[B') {
      if (this.showPopup) {
        const filtered = this.getPopupItems();
        if (this.popupIndex < filtered.length - 1) {
          this.popupIndex++;
          this.render();
        }
      } else {
        // Scroll down
        if (this.scrollOffset > 0) {
          this.scrollOffset--;
          this.render();
        }
      }
      return;
    }

    if (key === '\t') {
      if (this.showPopup) {
        const filtered = this.getPopupItems();
        if (filtered.length > 0) {
          this.applyCompletion(filtered[this.popupIndex].name);
          // Add trailing space so next level popup triggers
          this.inputBuffer += ' ';
          // Check if there are more completion levels
          const next = this.getPopupItems();
          if (next.length > 0) {
            this.showPopup = true;
            this.popupIndex = 0;
          } else {
            // Remove trailing space if no more completions
            this.inputBuffer = this.inputBuffer.trimEnd();
            this.showPopup = false;
            this.popupIndex = 0;
          }
          this.render();
        }
      } else {
        this.showPopup = true;
        this.popupIndex = 0;
        this.render();
      }
      return;
    }

    // Printable characters
    if (key >= ' ' && key.length === 1) {
      this.inputBuffer += key;
      this.updatePopup();
      this.render();
    }
  };

  private applyCompletion(selected: string): void {
    if (this.popupMode === 'args') {
      // Keep everything up to the current arg level and replace the last part
      const raw = this.inputBuffer.startsWith('/') ? this.inputBuffer.slice(1) : this.inputBuffer;
      const parts = raw.trimStart().split(/\s+/).filter(p => p.length > 0);
      // Replace at the current depth level (popupDepth is 1-indexed)
      const prefix = parts.slice(0, this.popupDepth).join(' ');
      this.inputBuffer = `${prefix} ${selected}`;
    } else {
      this.inputBuffer = selected;
    }
  }

  private updatePopup(): void {
    if (this.inputBuffer.startsWith('/')) {
      this.showPopup = true;
      this.popupIndex = 0;
    } else if (this.inputBuffer.length === 0) {
      this.showPopup = false;
      this.popupIndex = 0;
    } else {
      // Check if we're typing args for a command
      const parts = this.inputBuffer.trimStart().split(/\s+/).filter(p => p.length > 0);
      const hasSpace = this.inputBuffer.includes(' ');
      if (parts.length >= 2 || (parts.length === 1 && hasSpace)) {
        const cmd = parts[0].toLowerCase();
        if (COMMANDS.find(c => c.name === cmd && c.hasArgs)) {
          this.showPopup = true;
          this.popupIndex = 0;
        }
      }
    }
    if (this.showPopup) {
      const filtered = this.getPopupItems();
      if (filtered.length === 0) {
        this.showPopup = false;
        this.popupIndex = 0;
      } else if (this.popupIndex >= filtered.length) {
        this.popupIndex = Math.max(0, filtered.length - 1);
      }
    }
  }
}
