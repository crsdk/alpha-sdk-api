#!/usr/bin/env pwsh
# ============================================================================
# crsdk - PowerShell wrapper for the bundled developer CLI (cli/).
#
# Mirrors the POSIX ./crsdk shim: build the CLI on first run, then forward to
# node. Forces UTF-8 output so the CLI's box-drawing and glyphs render on
# Windows PowerShell / Windows Terminal instead of showing as mojibake.
#
# Usage:  .\crsdk.ps1 doctor      (or, with crsdk.cmd present:  .\crsdk doctor)
# ============================================================================
$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }
try { $OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$here  = Split-Path -Parent $MyInvocation.MyCommand.Path
$cli   = Join-Path $here 'cli'
$entry = Join-Path $cli 'dist/bin/crsdk.js'

if (-not (Test-Path $entry)) {
  [Console]::Error.WriteLine('Building the crsdk CLI (first run)...')
  Push-Location $cli
  try {
    npm install
    if ($LASTEXITCODE -ne 0) { exit 1 }
    npm run build
    if ($LASTEXITCODE -ne 0) { exit 1 }
  } finally { Pop-Location }
}

node $entry @args
exit $LASTEXITCODE
