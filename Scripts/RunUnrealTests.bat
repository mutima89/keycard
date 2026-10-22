@echo off
rem Runs Unreal automation tests under the Keycard. prefix, headless, and prints the results.
rem Usage: RunUnrealTests.bat "C:\Program Files\Epic Games\UE_5.8"
setlocal
if "%~1"=="" (echo Usage: %~nx0 ^<UE install dir^> & exit /b 2)
set ROOT=%~dp0..
"%~1\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%ROOT%\Keycard.uproject" ^
  -ExecCmds="Automation RunTests Keycard.;Quit" -unattended -nullrhi -nopause -log=KcTests.log -ReportExportPath="%ROOT%\Saved\TestReport"
set RESULT=%ERRORLEVEL%
findstr /C:"Test Completed" /C:"TEST COMPLETE" "%ROOT%\Saved\Logs\KcTests.log"
exit /b %RESULT%
