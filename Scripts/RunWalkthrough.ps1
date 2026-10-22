# Runs the full walkthrough against a build: the whole career (M001 to the capstone X001, the ending and the
# certificate), then a resume of that save, then a daily challenge with its leaderboard.
#   -Exe   : packaged Keycard.exe (e.g. Release\Shipping\Windows\Keycard.exe); omit to use the editor in -game mode
#   -UE    : UE install dir (editor mode only)
#   -Shipping : the exe is a Shipping build in a folder not named Shipping (saves go to %LOCALAPPDATA%)
# Results: <Saved>\KcSelfTest_career.txt, KcSelfTest_challenge.txt and Saved\Screenshots\<platform>\Career_*.png, Challenge_*.png.
# No input is sent to the game: every step goes through the HUD calls the UI uses.
param(
    [string]$Exe = "",
    [string]$UE = "C:\Program Files\Epic Games\UE_5.8",
    [int]$TimeoutMinutes = 60,
    [switch]$Shipping
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if ($Exe -ne "") {
    $Saved = if ($Shipping -or $Exe -match "Shipping") { Join-Path $env:LOCALAPPDATA "Keycard\Saved" } else { Join-Path (Split-Path -Parent $Exe) "Keycard\Saved" }
    $Launch = { param($Mode) Start-Process -PassThru $Exe -ArgumentList "-KcSelfTest=$Mode", "-windowed", "-ResX=1920", "-ResY=1080" }
} else {
    $Saved = Join-Path $Root "Saved"
    $Launch = { param($Mode) Start-Process -PassThru "$UE\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList "`"$Root\Keycard.uproject`"", "-game", "-windowed", "-ResX=1920", "-ResY=1080", "-nosplash", "-KcSelfTest=$Mode" }
}
# Only the self-test's own files (slot "selftest"); the player's saves are never touched.
Remove-Item "$Saved\KcSelfTest_career.txt", "$Saved\KcSelfTest_challenge.txt", "$Saved\SaveGames\selftest*" -ErrorAction SilentlyContinue
$Failed = 0
foreach ($Mode in @("career", "challenge")) {
    $P = & $Launch $Mode
    if (-not $P.WaitForExit($TimeoutMinutes * 60000)) { Write-Output "${Mode}: timed out; stopping it"; $P.Kill(); $Failed++ }
    $Log = "$Saved\KcSelfTest_$Mode.txt"
    if (Test-Path $Log) {
        $Lines = Get-Content $Log
        Write-Output "${Mode}: $($Lines[0])"
        $Lines | Where-Object { $_ -like "FAIL *" } | ForEach-Object { Write-Output "  $_" }
        if ($Lines[0] -ne "PASS") { $Failed++ }
    } else { Write-Output "${Mode}: no log written"; $Failed++ }
}
Write-Output "Saved: $Saved"
exit $Failed
