@echo off
rem =====================================================================================================
rem  Compiles one of the mod's maps from source and puts the result where the game loads maps from.
rem
rem  Usage: Compile.bat <mapname> [moddir]
rem
rem    Compile.bat startup
rem      reads  <mod>\mapsrc\startup.vmf
rem      writes <mod>\maps\startup.bsp
rem
rem  The mod folder defaults to Mods\lambda inside the game repository GameDir.txt points at. Pass a
rem  second argument to compile a different mod - the content root under C:\XMT, for instance.
rem =====================================================================================================
setlocal
call "%~dp0Env.bat" || exit /b 1

set "MAP=%~1"
if "%MAP%"=="" (
	echo Usage: Compile.bat ^<mapname^> [moddir]
	exit /b 1
)

set "MOD=%~2"
if "%MOD%"=="" set "MOD=%MOD_DIR%"
set "EXE=%BIN_DIR%\LambdaBSP.exe"

if not exist "%EXE%" (
	echo [LambdaBSP] %EXE% is not built yet - run Build.bat first.
	exit /b 1
)
if not exist "%MOD%\mapsrc\%MAP%.vmf" (
	echo [LambdaBSP] No such map: %MOD%\mapsrc\%MAP%.vmf
	exit /b 1
)
if not exist "%MOD%\maps" mkdir "%MOD%\maps"

rem  -game is what makes the texture sizes in the BSP right: it mounts the same content the game will.
"%EXE%" "%MOD%\mapsrc\%MAP%.vmf" -game "%MOD%" -o "%MOD%\maps\%MAP%.bsp"
if errorlevel 1 exit /b 1

endlocal
