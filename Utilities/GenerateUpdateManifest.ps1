param(
    [Parameter(Mandatory = $true)][string]$AssetDirectory,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw 'Version must be three numeric components, for example 1.2.2.'
}
if (Test-Path -LiteralPath $OutputPath) {
    throw "Output file already exists: $OutputPath. Choose a new path and review it before deployment."
}

$files = [ordered]@{
    'windows-x64' = 'LightHostSetup-windows-x64.exe'
    'windows-x64-package' = 'LightHost-windows-x64.exe'
    'windows-arm64' = 'LightHostSetup-windows-arm64.exe'
    'windows-arm64-package' = 'LightHost-windows-arm64.exe'
    'linux-x64-package' = 'LightHost-linux-x64.zip'
    'linux-arm64-package' = 'LightHost-linux-arm64.zip'
    'macos-x64' = 'LightHost-macos-x64.zip'
    'macos-arm64' = 'LightHost-macos-arm64.zip'
}
$assets = [ordered]@{}
foreach ($key in $files.Keys) {
    $name = $files[$key]
    $path = Join-Path $AssetDirectory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing release asset: $path"
    }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    $assets[$key] = [ordered]@{
        url = "https://github.com/Anna-Spanner/LightHost/releases/download/v$Version/$name"
        sha256 = $hash
    }
}
$manifest = [ordered]@{ version = $Version; assets = $assets } | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText(
    [System.IO.Path]::GetFullPath($OutputPath), $manifest + [Environment]::NewLine,
    (New-Object System.Text.UTF8Encoding($false))
)
Write-Output "Manifest created: $OutputPath. Upload matching assets before deploying it."
