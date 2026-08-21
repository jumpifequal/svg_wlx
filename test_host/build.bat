@echo off
rem Build test_host.exe from an ordinary command prompt (finds the MSVC
rem x64 dev environment via vswhere, then compiles).
rem
rem Note: vswhere.exe lives under "Program Files (x86)", and cmd's `for /f`
rem backquote parser breaks on the literal "(x86)" in that path even when
rem quoted - so its output is captured via a temp file instead of `for /f`.
setlocal
set "VCVARS_TXT=%TEMP%\svg_wlx_vcvars_path.txt"
"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find "VC\Auxiliary\Build\vcvars64.bat" > "%VCVARS_TXT%"
set /p VCVARS=<"%VCVARS_TXT%"
del "%VCVARS_TXT%"
call "%VCVARS%"
cl /EHsc /nologo "%~dp0test_host.cpp" gdi32.lib user32.lib /Fe:"%~dp0test_host.exe"
