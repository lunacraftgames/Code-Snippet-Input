[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$packageName = 'CodeSnippetInput-Portable-win-x64'
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$releaseRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'releases'))
$stageRoot = [IO.Path]::GetFullPath((Join-Path $releaseRoot '.package-staging'))
$packageRoot = Join-Path $stageRoot $packageName
$managerOutput = Join-Path $packageRoot 'publish\manager-context'
$nativeBuild = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build\package-tsf-x64'))
$nativeDll = Join-Path $nativeBuild 'Release\CodeSnippetInputTsf.dll'
$newZip = Join-Path $releaseRoot "$packageName.new.zip"
$finalZip = Join-Path $releaseRoot "$packageName.zip"

function Assert-ChildPath {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $Parent
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\')
    if (-not $fullPath.StartsWith($fullParent + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe path outside the expected directory: $fullPath"
    }
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [string[]] $Arguments
    )

    Write-Host "> $FilePath $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath"
    }
}

function Find-CMake {
    $command = Get-Command 'cmake.exe' -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $matches = @(& $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
        $match = $matches | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
        if ($null -ne $match) {
            return $match
        }
    }

    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\CMake\bin\cmake.exe'
    )

    $candidate = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if ($null -ne $candidate) {
        return $candidate
    }

    throw 'CMake was not found. Install Visual Studio Build Tools with the C++ and CMake components.'
}

function Get-ZipEntryHash {
    param(
        [Parameter(Mandatory)] [System.IO.Compression.ZipArchive] $Archive,
        [Parameter(Mandatory)] [string] $EntryName
    )

    $entry = $Archive.Entries |
        Where-Object { $_.FullName.Replace('\', '/') -eq $EntryName } |
        Select-Object -First 1
    if ($null -eq $entry) {
        throw "Required ZIP entry is missing: $EntryName"
    }

    $stream = $entry.Open()
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

Assert-ChildPath -Path $stageRoot -Parent $releaseRoot
Assert-ChildPath -Path $newZip -Parent $releaseRoot
Assert-ChildPath -Path $finalZip -Parent $releaseRoot

$dotnet = Get-Command 'dotnet.exe' -ErrorAction SilentlyContinue
if ($null -eq $dotnet) {
    throw '.NET SDK was not found. Install the .NET 8 SDK before packaging.'
}
$cmake = Find-CMake

New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null

try {
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
    if (Test-Path -LiteralPath $newZip) {
        Remove-Item -LiteralPath $newZip -Force
    }

    New-Item -ItemType Directory -Path $managerOutput -Force | Out-Null

    Write-Host 'Building the Windows x64 TSF input method...' -ForegroundColor Cyan
    Invoke-NativeCommand -FilePath $cmake -Arguments @(
        '-S', (Join-Path $projectRoot 'CodeSnippetInputTsf'),
        '-B', $nativeBuild,
        '-A', 'x64'
    )
    Invoke-NativeCommand -FilePath $cmake -Arguments @(
        '--build', $nativeBuild,
        '--config', 'Release'
    )

    Write-Host 'Publishing the self-contained Windows x64 manager...' -ForegroundColor Cyan
    Invoke-NativeCommand -FilePath $dotnet.Source -Arguments @(
        'publish', (Join-Path $projectRoot 'CodeSnippetInput\CodeSnippetInput.csproj'),
        '-c', 'Release',
        '-r', 'win-x64',
        '--self-contained', 'true',
        '-p:PublishSingleFile=false',
        '-o', $managerOutput
    )

    if (-not (Test-Path -LiteralPath $nativeDll -PathType Leaf)) {
        throw "The native input-method DLL was not produced: $nativeDll"
    }

    Write-Host 'Assembling the portable package...' -ForegroundColor Cyan
    $tsfOutput = Join-Path $packageRoot 'publish\tsf-x64'
    $scriptOutput = Join-Path $packageRoot 'CodeSnippetInputTsf'
    New-Item -ItemType Directory -Path $tsfOutput, $scriptOutput -Force | Out-Null
    Copy-Item -LiteralPath $nativeDll -Destination (Join-Path $tsfOutput 'CodeSnippetInputTsf.dll') -Force

    foreach ($file in @('install-input-method.ps1', 'uninstall-input-method.ps1', 'README.md')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot "CodeSnippetInputTsf\$file") -Destination $scriptOutput -Force
    }

    foreach ($file in @(
        'install-input-method.bat',
        'uninstall-input-method.bat',
        'open-manager.bat',
        'cleanup-old-versions.bat',
        'README.md',
        'README.zh-CN.md',
        'README.en-US.md',
        'LICENSE',
        '便携安装包使用说明.txt'
    )) {
        Copy-Item -LiteralPath (Join-Path $projectRoot $file) -Destination $packageRoot -Force
    }

    $requiredFiles = @(
        'publish\manager-context\CodeSnippetInput.exe',
        'publish\manager-context\CodeSnippetInput.dll',
        'publish\tsf-x64\CodeSnippetInputTsf.dll',
        'CodeSnippetInputTsf\install-input-method.ps1',
        'CodeSnippetInputTsf\uninstall-input-method.ps1',
        'install-input-method.bat',
        'uninstall-input-method.bat',
        'open-manager.bat',
        'README.md',
        'README.zh-CN.md',
        'README.en-US.md',
        'LICENSE'
    )
    $missingFiles = @($requiredFiles | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $packageRoot $_) -PathType Leaf)
    })
    if ($missingFiles.Count -gt 0) {
        throw "Required package files are missing: $($missingFiles -join ', ')"
    }

    foreach ($script in @('install-input-method.bat', 'uninstall-input-method.bat', 'open-manager.bat')) {
        if (Select-String -LiteralPath (Join-Path $packageRoot $script) -SimpleMatch 'tsf-x64-literal' -Quiet) {
            throw "$script still refers to the obsolete tsf-x64-literal directory."
        }
    }

    Write-Host 'Creating and verifying the ZIP archive...' -ForegroundColor Cyan
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $stageRoot,
        $newZip,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false
    )

    $managerPath = Join-Path $packageRoot 'publish\manager-context\CodeSnippetInput.exe'
    $tsfPath = Join-Path $packageRoot 'publish\tsf-x64\CodeSnippetInputTsf.dll'
    $managerHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $managerPath).Hash
    $tsfHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $tsfPath).Hash
    $managerEntry = "$packageName/publish/manager-context/CodeSnippetInput.exe"
    $tsfEntry = "$packageName/publish/tsf-x64/CodeSnippetInputTsf.dll"

    $archive = [System.IO.Compression.ZipFile]::OpenRead($newZip)
    try {
        $entryNames = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        $requiredEntries = @(
            $managerEntry,
            $tsfEntry,
            "$packageName/install-input-method.bat",
            "$packageName/uninstall-input-method.bat",
            "$packageName/open-manager.bat",
            "$packageName/README.md",
            "$packageName/README.zh-CN.md",
            "$packageName/README.en-US.md",
            "$packageName/LICENSE"
        )
        $missingEntries = @($requiredEntries | Where-Object { $_ -notin $entryNames })
        if ($missingEntries.Count -gt 0) {
            throw "Required ZIP entries are missing: $($missingEntries -join ', ')"
        }
        if ($entryNames -match '/publish/tsf-x64-literal/') {
            throw 'The ZIP archive contains the obsolete tsf-x64-literal directory.'
        }
        if ((Get-ZipEntryHash -Archive $archive -EntryName $managerEntry) -ne $managerHash) {
            throw 'The manager hash does not match inside the ZIP archive.'
        }
        if ((Get-ZipEntryHash -Archive $archive -EntryName $tsfEntry) -ne $tsfHash) {
            throw 'The TSF DLL hash does not match inside the ZIP archive.'
        }
        $entryCount = $archive.Entries.Count
    }
    finally {
        $archive.Dispose()
    }

    [IO.File]::Copy($newZip, $finalZip, $true)
    $newZipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $newZip).Hash
    $finalZipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $finalZip).Hash
    if ($newZipHash -ne $finalZipHash) {
        throw 'The final ZIP hash does not match the verified temporary ZIP.'
    }

    $zipInfo = Get-Item -LiteralPath $finalZip
    Write-Host ''
    Write-Host 'Portable package created successfully.' -ForegroundColor Green
    Write-Host "Path:    $($zipInfo.FullName)"
    Write-Host "Size:    $($zipInfo.Length) bytes"
    Write-Host "Files:   $entryCount"
    Write-Host "SHA-256: $finalZipHash"
}
finally {
    if (Test-Path -LiteralPath $newZip) {
        Remove-Item -LiteralPath $newZip -Force
    }
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
}
