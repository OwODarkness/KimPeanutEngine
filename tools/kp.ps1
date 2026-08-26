[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("status", "configure", "build", "test", "validate", "smoke", "loc", "help")]
    [string]$Command = "validate",

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$CommandArgs
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $RepoRoot "build"
$BuildConfig = "Debug"

function Show-Usage {
    @"
KimPeanutEngine command wrapper

Usage:
  .\tools\kp.ps1 status
  .\tools\kp.ps1 configure
  .\tools\kp.ps1 build [target]
  .\tools\kp.ps1 test [CTest-regex]
  .\tools\kp.ps1 validate [changed-file ...]
  .\tools\kp.ps1 smoke
  .\tools\kp.ps1 loc

Examples:
  .\tools\kp.ps1 validate
  .\tools\kp.ps1 validate engine/runtime/render/render_scene.cpp
  .\tools\kp.ps1 build RenderPassScheduleTest
  .\tools\kp.ps1 test RenderPassScheduleTest
"@
}

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    Write-Host ("> {0} {1}" -f $Executable, ($Arguments -join " ")) -ForegroundColor DarkGray
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("Command failed with exit code {0}: {1}" -f $LASTEXITCODE, $Executable)
    }
}

function Ensure-BuildTree {
    if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
        Invoke-External "cmake" @("-S", $RepoRoot, "-B", $BuildDir, "-G", "Visual Studio 17 2022")
    }
}

function Invoke-BuildTarget {
    param([Parameter(Mandatory = $true)][string]$Target)
    Ensure-BuildTree
    Invoke-External "cmake" @("--build", $BuildDir, "--config", $BuildConfig, "--target", $Target)
}

function Get-ChangedFiles {
    $tracked = @(& git -C $RepoRoot diff --name-only --diff-filter=ACMR)
    $untracked = @(& git -C $RepoRoot ls-files --others --exclude-standard)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect Git changes."
    }

    return @($tracked + $untracked | Where-Object {
        $_ -and $_.Trim() -and
        $_ -notmatch '^(build|build-opengl-only|third_party|logs)(\\|/)' -and
        $_ -notmatch '\.(obj|pdb|exe|dll|lib|tlog)$'
    } | ForEach-Object {
        $_.Replace("/", "\")
    } | Select-Object -Unique)
}

function Add-UniqueValue {
    param(
        [Parameter(Mandatory = $true)][System.Collections.ArrayList]$List,
        [Parameter(Mandatory = $true)][string]$Value
    )
    if (-not $List.Contains($Value)) {
        [void]$List.Add($Value)
    }
}

function Get-ValidationPlan {
    param([string[]]$Files)

    $targets = [System.Collections.ArrayList]::new()
    $tests = [System.Collections.ArrayList]::new()
    $areas = [System.Collections.ArrayList]::new()
    $reconfigure = $false
    $fullBuild = $false
    $needsSmoke = $false
    $codeChange = $false

    foreach ($file in $Files) {
        $path = $file.Replace("/", "\")

        if ($path -match '^(docs\\|README|AGENTS\.md$|Agent[_-]TODO\.md$|\.claude\\|\.codex\\)') {
            continue
        }

        $codeChange = $true

        if ($path -match '(^|\\)CMakeLists\.txt$|^cmake\\') {
            $reconfigure = $true
            $fullBuild = $true
        }

        if ($path -match '^engine\\runtime\\core\\') {
            Add-UniqueValue $areas "core"
        }
        if ($path -match '^engine\\runtime\\asset\\') {
            Add-UniqueValue $areas "asset"
            Add-UniqueValue $targets "AssetExample"
        }
        if ($path -match '^engine\\runtime\\audio\\') {
            Add-UniqueValue $areas "audio"
            Add-UniqueValue $targets "AudioUnitTest"
            Add-UniqueValue $tests "AudioUnitTest"
        }
        if ($path -match '^engine\\runtime\\script\\') {
            Add-UniqueValue $areas "script"
            Add-UniqueValue $targets "ScriptUnitTest"
            Add-UniqueValue $tests "ScriptUnitTest"
        }
        if ($path -match '^engine\\module\\tts\\') {
            Add-UniqueValue $areas "tts"
            Add-UniqueValue $targets "TTSExample"
        }
        if ($path -match '^engine\\runtime\\graphics\\') {
            Add-UniqueValue $areas "graphics"
            Add-UniqueValue $targets "GraphicsContractTest"
            Add-UniqueValue $tests "GraphicsContractTest"
        }
        if ($path -match '^engine\\runtime\\graphics\\backend\\') {
            $needsSmoke = $true
        }
        if ($path -match '^engine\\runtime\\render\\') {
            Add-UniqueValue $areas "render"
            Add-UniqueValue $targets "RenderPassScheduleTest"
            Add-UniqueValue $tests "RenderPassScheduleTest"
            $needsSmoke = $true
        }
        if ($path -match '^engine\\editor\\') {
            Add-UniqueValue $areas "editor"
            Add-UniqueValue $targets "KimPeanutEngine"
        }
        if ($path -match '^engine\\example\\') {
            Add-UniqueValue $areas "examples"
        }
        if ($path -match '^engine\\example\\graphics\\') {
            Add-UniqueValue $targets "GraphicsSmoke"
            $needsSmoke = $true
        }
        if ($path -match '^engine\\example\\asset\\') {
            Add-UniqueValue $targets "AssetExample"
        }
        if ($path -match '^engine\\example\\audio\\') {
            Add-UniqueValue $targets "AudioExample"
        }
        if ($path -match '^engine\\example\\tts\\') {
            Add-UniqueValue $targets "TTSExample"
        }
        if ($path -match '^engine\\module\\' -and $path -notmatch '^engine\\module\\tts\\') {
            Add-UniqueValue $areas "module"
            $fullBuild = $true
        }

        if ($path -match '\.(h|hpp|inl)$' -and $path -match '^engine\\runtime\\(graphics\\backend\\common|render|core\\base)\\') {
            $fullBuild = $true
        }
    }

    if ($needsSmoke) {
        Add-UniqueValue $targets "GraphicsSmoke"
    }

    if ($codeChange -and $areas.Count -eq 0) {
        Add-UniqueValue $areas "unknown"
        $fullBuild = $true
    }

    [pscustomobject]@{
        Files = $Files
        Areas = @($areas)
        Targets = @($targets)
        Tests = @($tests)
        Reconfigure = $reconfigure
        FullBuild = $fullBuild
        NeedsSmoke = $needsSmoke
        CodeChange = $codeChange
    }
}

function Invoke-ValidationPlan {
    param(
        [Parameter(Mandatory = $true)]$Plan
    )

    if (-not $Plan.CodeChange) {
        Write-Host "Documentation/workflow-only change; C++ validation skipped." -ForegroundColor Cyan
        return
    }

    Write-Host "Changed areas: $($Plan.Areas -join ', ')" -ForegroundColor Cyan
    if ($Plan.Reconfigure) {
        Invoke-External "cmake" @("-S", $RepoRoot, "-B", $BuildDir, "-G", "Visual Studio 17 2022")
    }

    foreach ($target in $Plan.Targets) {
        Invoke-BuildTarget $target
    }

    foreach ($test in $Plan.Tests) {
        Ensure-BuildTree
        Invoke-External "ctest" @("--test-dir", $BuildDir, "-C", $BuildConfig, "-R", $test, "--output-on-failure")
    }

    if ($Plan.NeedsSmoke) {
        Invoke-Smoke
    }

    if ($Plan.FullBuild) {
        Write-Host "Broad-impact change detected; running full build and test suite." -ForegroundColor Yellow
        Ensure-BuildTree
        Invoke-External "cmake" @("--build", $BuildDir, "--config", $BuildConfig)
        Invoke-External "ctest" @("--test-dir", $BuildDir, "-C", $BuildConfig, "--output-on-failure")
    }

    Write-Host "Validation passed." -ForegroundColor Green
}

function Invoke-Smoke {
    Invoke-BuildTarget "GraphicsSmoke"
    $exe = Get-ChildItem -Path $BuildDir -Filter "GraphicsSmoke.exe" -File -Recurse | Select-Object -First 1
    if (-not $exe) {
        throw "GraphicsSmoke.exe was built but could not be located under $BuildDir."
    }
    Invoke-External $exe.FullName @()
}

function Show-Loc {
    $extensions = @("*.cpp", "*.h", "*.hpp", "*.c", "*.cc", "*.inl")
    $files = Get-ChildItem (Join-Path $RepoRoot "engine") -Recurse -File -Include $extensions
    $lines = ($files | ForEach-Object { (Get-Content -LiteralPath $_.FullName).Count } | Measure-Object -Sum).Sum
    Write-Host ("C/C++ files: {0}" -f $files.Count)
    Write-Host ("Lines:        {0}" -f $lines)
    Write-Host "Scope: engine/ only; excludes third_party and build output."
}

switch ($Command) {
    "help" {
        Show-Usage
    }
    "status" {
        Write-Host ("Repository: {0}" -f $RepoRoot)
        & git -C $RepoRoot status --short --branch
        Write-Host ""
        Get-Content (Join-Path $RepoRoot "docs/status.md")
    }
    "configure" {
        Invoke-External "cmake" @("-S", $RepoRoot, "-B", $BuildDir, "-G", "Visual Studio 17 2022")
    }
    "build" {
        if ($CommandArgs.Count -gt 0) {
            Invoke-BuildTarget $CommandArgs[0]
        }
        else {
            Ensure-BuildTree
            Invoke-External "cmake" @("--build", $BuildDir, "--config", $BuildConfig)
        }
    }
    "test" {
        Ensure-BuildTree
        if ($CommandArgs.Count -gt 0) {
            Invoke-External "ctest" @("--test-dir", $BuildDir, "-C", $BuildConfig, "-R", $CommandArgs[0], "--output-on-failure")
        }
        else {
            Invoke-External "ctest" @("--test-dir", $BuildDir, "-C", $BuildConfig, "--output-on-failure")
        }
    }
    "validate" {
        $files = if ($CommandArgs.Count -gt 0) { $CommandArgs } else { Get-ChangedFiles }
        if ($files.Count -eq 0) {
            Write-Host "No changed files detected. Use validate <path> to validate a specific change." -ForegroundColor Yellow
        }
        else {
            Invoke-ValidationPlan (Get-ValidationPlan $files)
        }
    }
    "smoke" {
        Invoke-Smoke
    }
    "loc" {
        Show-Loc
    }
}
