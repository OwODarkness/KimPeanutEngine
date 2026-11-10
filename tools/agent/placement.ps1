[CmdletBinding()]
param(
    [string[]]$Paths,
    [string]$Pattern
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path

function Read-Git {
    param([string[]]$GitArgs)
    $result = @(& git -c core.safecrlf=false -c core.quotePath=false -C $repo @GitArgs)
    if ($LASTEXITCODE -ne 0) { throw "Git inspection failed: $($GitArgs -join ' ')" }
    return $result
}

Push-Location $repo
try {
    $changed = @(Read-Git @('diff', 'HEAD', '--name-only', '--diff-filter=ACMR'))
    $untracked = @(Read-Git @('ls-files', '--others', '--exclude-standard'))
    $files = if ($Paths) {
        foreach ($path in $Paths) {
            $resolved = (Resolve-Path -LiteralPath $path).Path
            if ($resolved -ne $repo -and -not $resolved.StartsWith($repo + [IO.Path]::DirectorySeparatorChar,
                    [StringComparison]::OrdinalIgnoreCase)) { throw "Path is outside repository: $path" }
            if (Test-Path -LiteralPath $resolved -PathType Container) {
                & rg --files --glob '*.{h,hpp,cpp,cc,cxx}' -- $path
                if ($LASTEXITCODE -gt 1) { throw "Source inventory failed: $path" }
            } else { $resolved }
        }
    } else { @($changed) + @($untracked) }
    $files = @($files | Where-Object { $_ -match '\.(h|hpp|cpp|cc|cxx)$' } |
        ForEach-Object { [IO.Path]::GetRelativePath($repo, (Resolve-Path -LiteralPath $_).Path).Replace('\', '/') } |
        Where-Object { $_ -notmatch '^(third_party|build[^/]*|save)/' } | Sort-Object -Unique)
    Write-Output "Placement evidence: $(Read-Git @('rev-parse', '--short', 'HEAD'))"
    Write-Output "Workflow: docs/agent_implementation_workflow.md"
    Write-Output "Scoped source files ($($files.Count)):"
    $files | Write-Output
    $guides = @('AGENTS.md')
    foreach ($file in $files) {
        $directory = Split-Path (Join-Path $repo $file) -Parent
        while ($directory -and $directory -ne $repo) {
            $guide = Join-Path $directory 'AGENTS.md'
            if (Test-Path -LiteralPath $guide) { $guides += [IO.Path]::GetRelativePath($repo, $guide) }
            $directory = Split-Path $directory -Parent
        }
    }
    Write-Output 'Agent guides:'
    $guides | Sort-Object -Unique | Write-Output
    if ($Pattern) {
        Write-Output "Repository symbol search: $Pattern"
        & rg -n --glob '*.{h,hpp,cpp,cc,cxx}' -- $Pattern engine
        if ($LASTEXITCODE -gt 1) { throw 'Symbol search failed.' }
        if ($LASTEXITCODE -eq 1) { Write-Output 'No matches; inspect neighboring domain operations.' }
    }
    $violations = @()
    foreach ($file in $files) {
        $lines = @(Get-Content -LiteralPath $file)
        if ($file -match '^engine/runtime/(render/|graphics/backend/common/)') {
            foreach ($line in $lines) {
                if ($line -match '^\s*#\s*include\s*[<"](?:[^">]*/)?(?:(?:vulkan|opengl)(?:/|_)|imgui[._/]|editor/)' -or
                    $line -match '^\s*#\s*include\s*[<"](?:vulkan/|GL/|glad/)') {
                    $violations += "${file}: forbidden backend/UI include: $line"
                }
            }
        }
        if ($file -eq 'engine/runtime/render/render_system.h' -and
            ($lines -match '#\s*include.*deferred_renderer\.h')) {
            $violations += "${file}: public facade includes concrete renderer"
        }
        if ($file -match '^engine/runtime/') {
            $added = if ($file -in $untracked) { $lines } else {
                @(Read-Git @('diff', 'HEAD', '--unified=0', '--', $file) |
                    Where-Object { $_ -match '^\+(?!\+)' } | ForEach-Object { $_.Substring(1) })
            }
            foreach ($line in $added) {
                if ($line -match '^\s*#\s*include\s*[<"](?:[^">]*/)?editor/') {
                    $violations += "${file}: new Runtime -> Editor include: $line"
                }
            }
        }
    }
    if ($violations.Count) {
        $violations | Write-Output
        throw "$($violations.Count) structural placement violation(s)."
    }
    Write-Output 'Structural checks passed. Semantic ownership/reuse review remains required.'
} finally { Pop-Location }
