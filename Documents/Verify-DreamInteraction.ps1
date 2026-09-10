param(
    [string]$EngineRoot,
    [switch]$BuildGame
)
# 从脚本自身位置定位项目，不依赖调用者当前目录或机器固定盘符。
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProjectFile = Join-Path $ProjectRoot 'DreamSpace.uproject'
if (-not $EngineRoot) {
    $Association = (Get-Content -Raw $ProjectFile | ConvertFrom-Json).EngineAssociation
    $EngineRoot = (Get-ItemProperty "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Association" -ErrorAction Stop).InstalledDirectory
}
$BuildTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$Editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $BuildTool) -or -not (Test-Path -LiteralPath $Editor)) { throw '找不到目标 Unreal Engine 工具。请使用 -EngineRoot 指定安装路径。' }
& $BuildTool DreamSpaceEditor Win64 Development "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE -NoUBA
if ($LASTEXITCODE -ne 0) { throw 'DreamSpaceEditor 编译失败。' }
if ($BuildGame) {
    & $BuildTool DreamSpace Win64 Development "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE -NoUBA
    if ($LASTEXITCODE -ne 0) { throw 'DreamSpace Game 目标编译失败。' }
}
$Report = Join-Path $ProjectRoot 'Saved\Automation\FinalInteraction'
$Log = Join-Path $ProjectRoot 'Saved\Logs\FinalInteractionTests.log'
& $Editor $ProjectFile /Engine/Maps/Entry -unattended -nop4 -nosplash -NullRHI -nosound '-ExecCmds=Automation RunTests DreamSpace' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$Report" "-abslog=$Log"
if ($LASTEXITCODE -ne 0) { throw "自动化验收失败，请查看 $Log" }
$Result = Get-Content -Raw (Join-Path $Report 'index.json') | ConvertFrom-Json
$Failed = @($Result.tests | Where-Object state -ne 'Success')
if ($Result.tests.Count -eq 0 -or $Failed.Count -gt 0) { throw '测试没有全部成功，请检查自动化报告。' }
Write-Output "通过 $($Result.tests.Count) 项验收。报告：$Report"
