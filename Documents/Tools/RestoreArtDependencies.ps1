param(
    [string]$ReferenceProject = 'F:\Ue5 Project\DreamSpace-master',
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$reference = [IO.Path]::GetFullPath($ReferenceProject)
$reportDir = Join-Path $project 'Saved\ArtRepair'
$referenceAudit = Get-Content -LiteralPath (Join-Path $reportDir 'reference.json') -Raw | ConvertFrom-Json -AsHashtable
$plan = @()
foreach ($package in ($referenceAudit.packages.Keys | Sort-Object)) {
    $relative = 'Content/' + $package.Substring(6)
    foreach ($extension in @('.uasset', '.umap', '.ubulk', '.uexp', '.uptnl')) {
        $source = Join-Path $reference ($relative + $extension)
        $target = Join-Path $project ($relative + $extension)
        if ((Test-Path -LiteralPath $source -PathType Leaf) -and !(Test-Path -LiteralPath $target)) {
            if ($extension -eq '.umap') { throw "Unexpected missing map: $package" }
            $plan += [pscustomobject]@{
                Package = $package
                RelativePath = $relative + $extension
                Length = (Get-Item -LiteralPath $source).Length
                SHA256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
            }
        }
    }
}
$plan | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportDir 'dependency-plan.json') -Encoding utf8
Write-Output "Missing dependency files: $($plan.Count); bytes: $(($plan | Measure-Object Length -Sum).Sum)"
if (!$Apply) { return }
if (Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
    throw 'Close Unreal Editor before copying packages.'
}
$backup = Join-Path $project 'Saved\ArtRepair\Backup-original'
if (Test-Path -LiteralPath $backup) { throw "Backup already exists: $backup" }
New-Item -ItemType Directory -Path $backup | Out-Null
$manifest = @()
foreach ($folder in @('Content', 'Config', 'Source')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $project $folder) -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($project, $file.FullName)
        $destination = Join-Path $backup $relative
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination
        $manifest += [pscustomobject]@{
            RelativePath = $relative
            SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        }
    }
}
foreach ($relative in @('.gitignore', 'DreamSpace.uproject')) {
    Copy-Item -LiteralPath (Join-Path $project $relative) -Destination (Join-Path $backup $relative)
    $manifest += [pscustomobject]@{
        RelativePath = $relative
        SHA256 = (Get-FileHash -LiteralPath (Join-Path $project $relative) -Algorithm SHA256).Hash
    }
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportDir 'original-file-hashes.json') -Encoding utf8
foreach ($entry in $plan) {
    $source = Join-Path $reference $entry.RelativePath
    $target = Join-Path $project $entry.RelativePath
    if (Test-Path -LiteralPath $target) { throw "Refusing to overwrite $target" }
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.SHA256) {
        throw "Copied file verification failed: $target"
    }
}
Write-Output "Copied $($plan.Count) files; originals backed up at $backup"
