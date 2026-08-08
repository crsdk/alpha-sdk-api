// =============================================================================
// CLI Visual Components — Styled output primitives
// =============================================================================

import { colors, symbols, stagger, isTTY } from './theme.js';

export async function printHeader(text: string): Promise<void> {
  console.log(`\n  ${symbols.diamond} ${colors.white(text)}`);
  await stagger(80);
}

export async function printCheck(
  status: 'pass' | 'fail' | 'warn',
  text: string,
  detail?: string,
): Promise<void> {
  const icon =
    status === 'pass' ? symbols.check :
    status === 'fail' ? symbols.cross :
    symbols.warn;
  const detailStr = detail ? ` ${colors.dim(detail)}` : '';
  console.log(`  ${icon} ${colors.text(text)}${detailStr}`);
  await stagger(50);
}

export async function printKV(
  key: string,
  value: string,
  opts?: { valueColor?: (s: string) => string; pad?: number },
): Promise<void> {
  const pad = opts?.pad ?? 14;
  const colorFn = opts?.valueColor ?? colors.text;
  console.log(`  ${colors.dim(key.padEnd(pad))}${colorFn(value)}`);
  await stagger(50);
}

export function printProgress(pct: number, label: string): void {
  const width = 30;
  const filled = Math.round((pct / 100) * width);
  const empty = width - filled;
  const bar = colors.accent('█'.repeat(filled)) + colors.border('░'.repeat(empty));
  console.log(`  ${bar} ${colors.text(`${pct}%`)}  ${colors.dim(label)}`);
}

export function printBox(lines: string[]): void {
  const maxLen = lines.reduce((m, l) => Math.max(m, l.length), 0);
  const w = Math.max(maxLen + 4, 30);
  console.log(`  ${colors.border('┌' + '─'.repeat(w) + '┐')}`);
  for (const line of lines) {
    console.log(`  ${colors.border('│')} ${line}${' '.repeat(w - line.length - 1)}${colors.border('│')}`);
  }
  console.log(`  ${colors.border('└' + '─'.repeat(w) + '┘')}`);
}

export function printDivider(): void {
  console.log(`  ${colors.border('─'.repeat(56))}`);
}

export function getTermWidth(): number {
  return Math.min(process.stdout.columns || 60, 72);
}

export function printFrame(label?: string): void {
  if (isTTY()) {
    // Move up one line and clear it — replaces the shell prompt line
    process.stdout.write('\x1b[1A\x1b[2K');
  }
  const w = getTermWidth();
  if (label) {
    const left = 2;
    const labelStr = ` ${label} `;
    const right = Math.max(0, w - left - labelStr.length);
    console.log(colors.border('─'.repeat(left) + labelStr + '─'.repeat(right)));
  } else {
    console.log(colors.border('─'.repeat(w)));
  }
}

export function printFrameEnd(): void {
  const w = getTermWidth();
  console.log(colors.border('─'.repeat(w)));
}

export async function printSection(label: string): Promise<void> {
  console.log(`\n  ${colors.muted(label)}`);
  await stagger(50);
}

export function printCommand(text: string): void {
  console.log(`  ${colors.accent('$')} ${colors.accent(text)}`);
}

export function printBlank(): void {
  console.log();
}

export function printSummary(passed: number, warnings: number, failed: number): void {
  const parts: string[] = [];
  if (passed > 0) parts.push(colors.green(`${passed} passed`));
  if (warnings > 0) parts.push(colors.yellow(`${warnings} warning${warnings > 1 ? 's' : ''}`));
  if (failed > 0) parts.push(colors.red(`${failed} failed`));
  console.log(`\n  ${parts.join(colors.dim(' · '))}`);
}
