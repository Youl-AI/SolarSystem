@echo off
REM Run with developer instrumentation enabled.
REM A normal launch has all of this compiled in but switched off.
REM
REM Comments here are ASCII on purpose: cmd.exe reads .bat files in the system
REM codepage, so UTF-8 text gets mangled and the broken bytes are then parsed
REM as commands.
REM
REM What this turns on:
REM   - the "Show GPU Times" entry in the settings window
REM     (per-pass GPU milliseconds, asteroid LOD counts, render target size)
REM   - a console summary every 120 frames, plus the [lodmix] line
REM   - texture loading diagnostics (format support, fp16 conversion accuracy)
REM
REM Run this from a command prompt if you want to keep the console output.

setlocal
set SOLAR_PROFILE=1

REM This script lives in scripts\, so step up to the repository root.
REM The executable resolves textures\ and shaders\ relatively and must start there.
cd /d "%~dp0.."

if not exist "build\Release\Ephemeris.exe" (
    echo build\Release\Ephemeris.exe not found. Build it first:
    echo   cmake --build build --config Release
    exit /b 1
)

build\Release\Ephemeris.exe %*
