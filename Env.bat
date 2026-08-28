@echo off
rem Shared environment for the LambdaBSP helper scripts.
set "PROJECT_DIR=%~dp0."
for %%I in ("%PROJECT_DIR%") do set "PROJECT_DIR=%%~fI"
rem The game files live in their own repository; GameDir.txt beside these scripts says where they are.
rem A GAME_DIR already set in the environment wins, so one build can be pointed elsewhere without
rem editing the file.
set "GAME_DIR_FROM=GameDir.txt"
if defined GAME_DIR set "GAME_DIR_FROM=the GAME_DIR environment variable"
if not defined GAME_DIR if exist "%PROJECT_DIR%\GameDir.txt" (
	for /f "usebackq eol=# tokens=* delims=" %%L in ("%PROJECT_DIR%\GameDir.txt") do (
		if not defined GAME_DIR if not "%%~L"=="" set "GAME_DIR=%%~L"
	)
)
if defined GAME_DIR for %%I in ("%GAME_DIR%") do set "GAME_DIR=%%~fI"

if not defined GAME_DIR (
	echo [LambdaBSP] No game directory. Put the path to your game repository in "%PROJECT_DIR%\GameDir.txt".
	exit /b 1
)
if not exist "%GAME_DIR%" (
	echo [LambdaBSP] %GAME_DIR_FROM% points at "%GAME_DIR%", which does not exist.
	exit /b 1
)

rem Where the game's binaries live, which is where LambdaBSP.exe is built to and run from.
set "BIN_DIR=%GAME_DIR%\LambdaEngine\Binaries\Win64"
rem The mod folder inside the game repository - the default for anything that takes one.
set "MOD_DIR=%GAME_DIR%\Mods\lambda"
