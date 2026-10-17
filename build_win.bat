@echo off
rem Windows build using Visual Studio 2022 (MSVC).
rem If cl.exe is not already on PATH, locate VS 2022 and load vcvars64.bat.
rem (cmd does not expand wildcards for the compiler, so the sources are listed here)
setlocal EnableExtensions EnableDelayedExpansion

where cl >nul 2>nul
if errorlevel 1 (
    set "VCVARS="
    for %%e in (Community Professional Enterprise BuildTools) do (
        if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat"
        )
    )

    if not defined VCVARS (
        echo error: Visual Studio 2022 with C++ tools was not found.
        exit /b 1
    )

    call "!VCVARS!" >nul || exit /b 1
)

python embed.py || exit /b 1

if not exist build mkdir build
set "SRCS="
for %%f in (src\*.cpp) do set "SRCS=!SRCS! %%f"

cl /nologo /utf-8 /std:c++20 /O2 /EHsc /Fo:build\\ /Fd:build\mbc.pdb !SRCS! /link /OUT:mbc.exe ws2_32.lib shell32.lib || exit /b 1
echo built mbc.exe
