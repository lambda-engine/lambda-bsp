@echo off
rem =====================================================================================================
rem  Builds LambdaBSP.exe and drops it next to the game binaries, where the launcher and any map-compile
rem  step can find it. Which game repository that is comes from GameDir.txt beside this script, not from
rem  where this folder happens to sit.
rem
rem  Usage: Build.bat [debug]
rem
rem  No dependencies beyond MSVC - LambdaBSP does not link against Unreal, and is not meant to. It is a
rem  command-line tool that reads a .vmf and writes a .bsp.
rem =====================================================================================================
setlocal
call "%~dp0Env.bat" || exit /b 1

set "SRC=%~dp0src"
set "OUTDIR=%BIN_DIR%"
set "OBJDIR=%~dp0Intermediate"

rem ---- find MSVC -------------------------------------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
	echo [LambdaBSP] vswhere.exe not found - is Visual Studio installed?
	exit /b 1
)
rem  Through a file rather than a for/f: the path to vswhere has spaces in it and a backquoted command that
rem  starts with a quote is re-parsed by cmd in a way that loses them.
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\lambdabsp_vspath.txt"
set "VSPATH="
if exist "%TEMP%\lambdabsp_vspath.txt" set /p VSPATH=<"%TEMP%\lambdabsp_vspath.txt"
del "%TEMP%\lambdabsp_vspath.txt" >nul 2>&1
if not defined VSPATH (
	echo [LambdaBSP] No Visual Studio with the C++ tools installed.
	exit /b 1
)
rem  Both streams: vcvarsall probes for a vswhere on PATH and complains when there is not one, which is noise
rem  from Microsoft's own script rather than anything wrong here. cl.exe is checked for directly afterwards, so
rem  silencing it cannot hide a real failure.
call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
where cl.exe >nul 2>&1
if errorlevel 1 (
	echo [LambdaBSP] MSVC did not come up - cl.exe is not on the path after vcvarsall.
	exit /b 1
)

rem ---- compile ---------------------------------------------------------------------------------------
if /i "%~1"=="debug" (
	set "CLFLAGS=/nologo /std:c++17 /EHsc /W4 /wd4100 /Zi /Od /MTd /D_CRT_SECURE_NO_WARNINGS"
) else (
	set "CLFLAGS=/nologo /std:c++17 /EHsc /W4 /wd4100 /O2 /MT /D_CRT_SECURE_NO_WARNINGS"
)

if not exist "%OBJDIR%" mkdir "%OBJDIR%"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

echo [LambdaBSP] Compiling...
cl %CLFLAGS% /Fo"%OBJDIR%\\" /Fd"%OBJDIR%\LambdaBSP.pdb" ^
	"%SRC%\main.cpp" "%SRC%\Compiler.cpp" "%SRC%\Winding.cpp" "%SRC%\KeyValues.cpp" ^
	"%SRC%\FileSystem.cpp" "%SRC%\Vpk.cpp" "%SRC%\Materials.cpp" "%SRC%\BspFile.cpp" ^
	/link /OUT:"%OUTDIR%\LambdaBSP.exe" /PDB:"%OBJDIR%\LambdaBSP.pdb"
if errorlevel 1 (
	echo [LambdaBSP] Build FAILED.
	exit /b 1
)

echo [LambdaBSP] -^> %OUTDIR%\LambdaBSP.exe
endlocal
