@echo off
rem ============================================================================
rem crsdk - Windows (cmd) wrapper for the bundled developer CLI (cli/).
rem
rem Mirrors the POSIX ./crsdk shim: build the CLI on first run, then forward to
rem node. The bash ./crsdk cannot run in cmd.exe / PowerShell, so this is the
rem Windows entry point. `chcp 65001` switches the console to UTF-8 so the CLI's
rem box-drawing and glyphs render instead of mojibake on legacy consoles.
rem
rem Usage (cmd or PowerShell):  .\crsdk doctor
rem ============================================================================
chcp 65001 >nul 2>&1
setlocal
set "HERE=%~dp0"
set "CLI=%HERE%cli"
set "ENTRY=%CLI%\dist\bin\crsdk.js"

if not exist "%ENTRY%" (
  echo Building the crsdk CLI ^(first run^)... 1>&2
  pushd "%CLI%"
  call npm install 1>&2
  if errorlevel 1 ( popd & exit /b 1 )
  call npm run build 1>&2
  if errorlevel 1 ( popd & exit /b 1 )
  popd
)

node "%ENTRY%" %*
exit /b %ERRORLEVEL%
