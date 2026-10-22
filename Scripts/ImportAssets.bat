@echo off
rem Imports Data/Assets (fetched by Scripts\FetchAssets.py) into Content/Keycard, headless.
rem Usage: ImportAssets.bat "C:\Program Files\Epic Games\UE_5.8"
setlocal
if "%~1"=="" (echo Usage: %~nx0 ^<UE install dir^> & exit /b 2)
set ROOT=%~dp0..
"%~1\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%ROOT%\Keycard.uproject" -run=pythonscript -script="%ROOT%\Scripts\ImportAssets.py" -unattended -nopause -log=KcImport.log
set RESULT=%ERRORLEVEL%
findstr /C:"KcImport:" "%ROOT%\Saved\Logs\KcImport.log"
rem Audio (built by Scripts\BuildAudio.py) into Content/Keycard/Audio.
"%~1\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%ROOT%\Keycard.uproject" -run=pythonscript -script="%ROOT%\Scripts\ImportAudio.py" -unattended -nopause -log=KcAudio.log
findstr /C:"KcAudio:" "%ROOT%\Saved\Logs\KcAudio.log"
exit /b %RESULT%
