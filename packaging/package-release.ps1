[CmdletBinding()]
param(
    [string]$Version,
    [string]$IsccPath,
    [string]$OutputRootDir,
    [ValidateRange(1, 128)][int]$Jobs = 6,
    [switch]$NoZip,
    [switch]$NoSetup
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Version) {
    $versionSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'version.iss') -Raw
    if ($versionSource -notmatch '#define MyAppVersion "([^"]+)"') { throw 'Missing release version.' }
    $Version = $Matches[1]
}
# major.minor.patch, with an optional pre-release suffix ("0.1.0-beta"); the
# update comparison inherited from Rise reads the numeric part only.
if ($Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$') { throw 'Use a major.minor.patch version, with an optional -suffix.' }
if (-not $OutputRootDir) { $OutputRootDir = Join-Path $projectRoot 'dist' }
$OutputRootDir = [IO.Path]::GetFullPath($OutputRootDir)
if (-not $NoSetup) {
    if (-not $IsccPath) {
        $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($compiler) { $IsccPath = $compiler.Source }
        foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, $env:LOCALAPPDATA)) {
            if (-not $base) { continue }
            foreach ($sub in @('Inno Setup 6/ISCC.exe', 'Programs/Inno Setup 6/ISCC.exe')) {
                $candidate = Join-Path $base $sub
                if (-not $IsccPath -and (Test-Path -LiteralPath $candidate)) { $IsccPath = $candidate }
            }
        }
    }
    if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
        throw 'Install Inno Setup 6.4+ or pass -IsccPath; -NoSetup builds only the portable package.'
    }
}
$buildRoot = Join-Path $projectRoot 'build/native'
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath)) { throw 'Run scripts/build-game-native.ps1 -Backend d3d12 first.' }
$cache = Get-Content -LiteralPath $cachePath -Raw
if ($cache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\s*$') { throw 'Packaging requires a Release build.' }
foreach ($option in @('BROKENBOND_NATIVE_RENDERER', 'BROKENBOND_NATIVE_D3D12')) {
    if ($cache -notmatch "(?m)^${option}:BOOL=(ON|TRUE|YES|1)\s*$") {
        throw "Packaging requires $option=ON. Configure scripts/build-game-native.ps1 -Backend d3d12."
    }
}
if ($cache -notmatch '(?m)^BROKENBOND_NATIVE_VULKAN:BOOL=OFF\s*$') {
    throw 'This release excludes Vulkan. Configure scripts/build-game-native.ps1 -Backend d3d12 before packaging.'
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) { throw 'Visual Studio C++ Build Tools are required.' }
    & (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}
& cmake --build $buildRoot --target broken_bond broken_bond_launcher --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Port build failed; previous packages were not changed.' }
$extractor = & (Join-Path $projectRoot 'scripts/get-extract-xiso.ps1')
$helper = Join-Path $projectRoot 'build/setup-helper/broken_bond_setup_helper.exe'
if (-not $NoSetup) { & (Join-Path $projectRoot 'scripts/build-setup-helper.ps1') -Jobs $Jobs }

$binaries = @('broken_bond.exe', 'broken_bond_launcher.exe', 'rexruntime.dll')
if (Test-Path -LiteralPath (Join-Path $buildRoot 'amd_fidelityfx_dx12.dll')) { $binaries += 'amd_fidelityfx_dx12.dll' }
$inspect = (Get-Command llvm-readobj.exe -ErrorAction Stop).Source
$peFiles = @($binaries | ForEach-Object { Join-Path $buildRoot $_ })
if (-not $NoSetup) { $peFiles += @($helper, $extractor) }
foreach ($path in $peFiles) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        throw "Missing or empty binary: $path"
    }
    $imports = & $inspect --coff-imports $path
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect binary: $path" }
    if ($imports -match 'Name:\s+(?:(?:MSVCP|VCRUNTIME|CONCRT)\w*D\.dll|ucrtbased\.dll)') {
        throw "Debug CRT dependency in $path. Rebuild the port and SDK in Release."
    }
    if (($imports -match 'Name:\s+amd_fidelityfx_dx12\.dll') -and
        ($binaries -notcontains 'amd_fidelityfx_dx12.dll')) { throw 'Missing FidelityFX runtime DLL.' }
}
$components = @((Import-PowerShellDataFile (Join-Path $PSScriptRoot 'third-party-licenses.psd1')).Components |
    Where-Object { -not $_.Condition -or ($_.Condition -eq 'FidelityFX' -and $binaries -contains 'amd_fidelityfx_dx12.dll') })
foreach ($component in $components) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $component.File))) {
        throw "Missing license for $($component.Name): $($component.File)"
    }
}

# Preserve failed staging for diagnosis and old releases for manual review.
$stage = Join-Path $OutputRootDir ('.packaging-' + [guid]::NewGuid().ToString('N'))
$portable = Join-Path $stage 'broken-bond-recomp-win-amd64'
New-Item -ItemType Directory -Path $portable -Force | Out-Null
foreach ($name in $binaries) { Copy-Item -LiteralPath (Join-Path $buildRoot $name) -Destination $portable }
Copy-Item -LiteralPath (Join-Path $projectRoot 'launcher/assets') -Destination $portable -Recurse
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'broken_bond.toml') -Destination $portable
Set-Content -LiteralPath (Join-Path $portable 'broken_bond.version') -Value $Version -Encoding ascii
$licenses = Join-Path $portable 'licenses'
New-Item -ItemType Directory -Path $licenses | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination (Join-Path $licenses 'broken-bond-recomp.txt')
foreach ($component in $components) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $component.File) -Destination (Join-Path $licenses $component.Target)
}
$notices = @('Third-party notices. Project code: GPL-3.0-only (broken-bond-recomp.txt).',
    'Game data, artwork and trademarks belong to their respective owners.', '')
foreach ($component in $components) {
    $notices += "$($component.Name): $($component.License) ($($component.Target))`n$($component.Url)`nUsed in: $($component.Usage)`n"
}
$notices += 'Source revisions (local SDK patches are in the project patches directory):'
foreach ($source in @('.', 'sdk', 'sdk/thirdparty/FFmpeg', 'sdk/thirdparty/libmspack')) {
    $revision = & git -C (Join-Path $projectRoot $source) rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw "Cannot record source revision: $source" }
    $notices += "${source}: $revision"
}
$notices += 'Build instructions and SDK patches: https://github.com/jorjbarbosa/broken-bond-recomp'
Set-Content -LiteralPath (Join-Path $licenses 'README.txt') -Value $notices -Encoding utf8
Set-Content -LiteralPath (Join-Path $portable 'README.txt') -Encoding utf8 -Value @"
Naruto: The Broken Bond PC port $Version

Start broken_bond_launcher.exe. Select your extracted game folder, or place it
in game/ beside the launcher. Setup can also import your original Xbox 360 ISO.
Supported Title ID: 55530825. The launcher and Setup verify default.xex SHA-256:
8f70e79443e36b38e44b6a105dad51fb8909fb615d982753cabaac9b15f0d576

Requires Windows x64, a Direct3D 12 GPU and the Microsoft Visual C++ x64 runtime.
This release includes Native Direct3D 12 only. No original game data is included.
Settings: broken_bond.toml. Saves/cache: user/ unless user_data_root is configured.
Updates and uninstall preserve configuration, game data and personal files.
See licenses/README.txt for third-party notices and source revisions.
Packaging success does not establish gameplay compatibility.
"@
if (-not $NoSetup) {
    $portBytes = (Get-ChildItem -LiteralPath $portable -Recurse -File | Measure-Object -Property Length -Sum).Sum
    & $IsccPath /Qp "/O$stage" "/DPackageDir=$portable" "/DHelperPath=$helper" "/DExtractorPath=$extractor" "/DPortBytes=$portBytes" "/DMyAppVersion=$Version" (Join-Path $PSScriptRoot 'installer.iss')
    if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed; staging preserved at $stage" }
}
if (-not $NoZip) {
    Compress-Archive -LiteralPath $portable -DestinationPath (Join-Path $stage 'broken-bond-recomp-win-amd64.zip')
}
$hashes = Get-ChildItem -LiteralPath $stage -File | Get-FileHash -Algorithm SHA256 |
    ForEach-Object { "$($_.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($_.Path))" }
Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Value $hashes -Encoding ascii
$backup = Join-Path $OutputRootDir ('previous-release-' + [guid]::NewGuid().ToString('N'))
foreach ($item in Get-ChildItem -LiteralPath $stage) {
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($item.FullName)) -ne $stage) {
        throw "Staged artifact outside the staging directory: $($item.FullName)"
    }
    $destination = Join-Path $OutputRootDir $item.Name
    if (Test-Path -LiteralPath $destination) {
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        # Both absolute paths are constructed directly below this output root.
        if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($destination)) -ne $OutputRootDir.TrimEnd('\', '/')) {
            throw "Destination outside the output root: $destination"
        }
        Move-Item -LiteralPath $destination -Destination $backup
    }
    Move-Item -LiteralPath $item.FullName -Destination $OutputRootDir
}
Remove-Item -LiteralPath $stage
Write-Host "Release artifacts: $OutputRootDir"
