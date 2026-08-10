// =============================================================================
// Athena Blue Theme — Color constants and symbols for CLI output
// =============================================================================

import chalk from 'chalk';

// Athena blue color palette
export const colors = {
  accent: chalk.hex('#4fb3ff'),
  accentDim: chalk.hex('#99e6ff'),
  accentBold: chalk.hex('#4fb3ff').bold,
  white: chalk.hex('#fafaf9').bold,
  text: chalk.hex('#fafaf9'),
  muted: chalk.hex('#a8a29e'),
  dim: chalk.hex('#78716c'),
  border: chalk.hex('#44403c'),
  green: chalk.hex('#34d399'),
  yellow: chalk.hex('#fbbf24'),
  red: chalk.hex('#f87171'),
  cyan: chalk.hex('#22d3ee'),
  orange: chalk.hex('#fb923c'),
};

export const symbols = {
  check: colors.green('✓'),
  cross: colors.red('✗'),
  warn: colors.yellow('!'),
  diamond: colors.accent('◆'),
  arrow: colors.dim('→'),
  bullet: colors.dim('·'),
  installed: colors.green('●'),
  added: colors.accent('◆'),
  skipped: colors.dim('○'),
  connected: colors.green('◆'),
  unreachable: colors.red('◇'),
  existing: colors.cyan('●'),
};

export function isTTY(): boolean {
  return process.stdout.isTTY === true;
}

export function stagger(ms: number): Promise<void> {
  if (!isTTY()) return Promise.resolve();
  return new Promise((resolve) => setTimeout(resolve, ms));
}
