[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("status", "configure", "build", "test", "validate", "smoke", "loc", "help")]
    [string]$Command = "validate",

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$CommandArgs,

    [Alias("Config")]
    [ValidateSet("Debug", "RelWithDebInfo", "Release", "MinSizeRel")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $RepoRoot "build"
$BuildConfig = $Configuration

if (-not ("KpCleanExternalProcess" -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class KpCleanExternalProcess {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct StartupInfo {
        public int cb;
        public string reserved, desktop, title;
        public int x, y, xSize, ySize, xChars, yChars, fill, flags;
        public short show, reserved2Size;
        public IntPtr reserved2, stdin, stdout, stderr;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInfo {
        public IntPtr process, thread;
        public int processId, threadId;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcess(string application, StringBuilder commandLine,
        IntPtr processAttributes, IntPtr threadAttributes, bool inheritHandles,
        uint creationFlags, IntPtr environment, string currentDirectory,
        ref StartupInfo startupInfo, out ProcessInfo processInfo);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);
    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr GetStdHandle(int standardHandle);

    private static string Quote(string value) {
        if (value.Length > 0 && value.IndexOfAny(new[] {' ', '\t', '"'}) < 0) return value;
        var result = new StringBuilder("\"");
        int slashes = 0;
        foreach (char character in value) {
            if (character == '\\') { slashes++; continue; }
            if (character == '"') result.Append('\\', slashes * 2 + 1);
            else result.Append('\\', slashes);
            result.Append(character);
            slashes = 0;
        }
        result.Append('\\', slashes * 2).Append('"');
        return result.ToString();
    }

    private static SortedDictionary<string, string> ReadEnvironment() {
        var values = new SortedDictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var pathParts = new List<string>();
        var pathPartSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (DictionaryEntry entry in Environment.GetEnvironmentVariables()) {
            string key = (string)entry.Key;
            string value = Convert.ToString(entry.Value);
            if (key.Equals("Path", StringComparison.OrdinalIgnoreCase)) {
                foreach (string part in value.Split(';'))
                    if (part.Length > 0 && pathPartSet.Add(part)) pathParts.Add(part);
                continue;
            }
            if (!values.ContainsKey(key)) values.Add(key, value);
        }
        values["Path"] = String.Join(";", pathParts);
        return values;
    }

    public static int Run(string application, string[] arguments, string currentDirectory) {
        var commandLine = new StringBuilder(Quote(application));
        foreach (string argument in arguments) commandLine.Append(' ').Append(Quote(argument));
        var environment = new StringBuilder();
        foreach (var entry in ReadEnvironment())
            environment.Append(entry.Key).Append('=').Append(entry.Value).Append('\0');
        environment.Append('\0');
        IntPtr environmentBlock = Marshal.StringToHGlobalUni(environment.ToString());
        var startup = new StartupInfo { cb = Marshal.SizeOf(typeof(StartupInfo)), flags = 0x100,
            stdin = GetStdHandle(-10), stdout = GetStdHandle(-11), stderr = GetStdHandle(-12) };
        ProcessInfo process;
        try {
            if (!CreateProcess(application, commandLine, IntPtr.Zero, IntPtr.Zero, true,
                0x400, environmentBlock, currentDirectory, ref startup, out process))
                throw new Win32Exception(Marshal.GetLastWin32Error());
        } finally { Marshal.FreeHGlobal(environmentBlock); }
        CloseHandle(process.thread);
        WaitForSingleObject(process.process, 0xffffffff);
        uint exitCode;
        if (!GetExitCodeProcess(process.process, out exitCode)) {
            int error = Marshal.GetLastWin32Error();
            CloseHandle(process.process);
            throw new Win32Exception(error);
        }
        CloseHandle(process.process);
        return unchecked((int)exitCode);
    }
}
'@
}

function Show-Usage {
    @"
KimPeanutEngine command wrapper

Usage:
  .\tools\kp.ps1 status
  .\tools\kp.ps1 configure
  .\tools\kp.ps1 [-Configuration <name>] build [target]
  .\tools\kp.ps1 test [CTest-regex]
  .\tools\kp.ps1 validate [changed-file ...]
  .\tools\kp.ps1 smoke
  .\tools\kp.ps1 loc

Examples:
  .\tools\kp.ps1 validate
  .\tools\kp.ps1 validate engine/runtime/render/render_scene.cpp
  .\tools\kp.ps1 build RenderPassScheduleTest
  .\tools\kp.ps1 -Configuration RelWithDebInfo build KimPeanutEngine
  .\tools\kp.ps1 test RenderPassScheduleTest
  .\tools\kp.ps1 test -l render
  .\tools\kp.ps1 test -l "render|graphics"
"@
}

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    Write-Host ("> {0} {1}" -f $Executable, ($Arguments -join " ")) -ForegroundColor DarkGray
    $resolvedExecutable = Get-Command $Executable -CommandType Application -ErrorAction Stop
    $exitCode = [KpCleanExternalProcess]::Run($resolvedExecutable.Source, $Arguments, $RepoRoot)
    if ($exitCode -ne 0) {
        throw ("Command failed with exit code {0}: {1}" -f $exitCode, $Executable)
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
        # PowerShell refuses to bind an empty collection to a mandatory
        # collection parameter, and every accumulator here starts empty, so
        # without this the first Add-UniqueValue call of any plan throws and
        # `kp.ps1 validate` never reaches a build.
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.ArrayList]$List,
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
        # Live2D owns test executables that compile the contract, mask planner,
        # and render planner directly, so a Live2D edit is validated by those
        # plus the Cubism-linked core test rather than by a full build. The
        # owning directories are module/live2d and test/unit/live2d; the latter
        # matches no other rule and would otherwise fall through to the
        # unknown-area branch below, which forces a full build.
        if ($path -match '^engine\\module\\live2d\\' -or $path -match '^engine\\test\\unit\\live2d\\') {
            Add-UniqueValue $areas "live2d"
            foreach ($live2dTest in @("Live2DRenderContractTest", "Live2DModelDataContractTest",
                    "Live2DRenderPlannerTest", "Live2DMaskPlannerTest", "Live2DCoreTest")) {
                Add-UniqueValue $targets $live2dTest
            }
            # One regex over the Live2D suite names, not one per executable. A
            # test executable is named for one of the suites it contains --
            # Live2DMaskPlannerTest also defines Live2DMaskAtlasPlannerTest, and
            # Live2DCoreTest also defines Live2DAssetTest, Live2DRendererTest,
            # and Live2DSettingsFixture -- so `-R <target>` silently skips the
            # rest of that binary's tests. `Live2D` matches all 52.
            Add-UniqueValue $tests "Live2D"
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
        if ($path -match '^engine\\module\\' -and $path -notmatch '^engine\\module\\(tts|live2d)\\') {
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
        # `test -l <module>` runs one module's tests by label; the module is the
        # directory under engine/test/unit, so render work can skip the rest.
        if ($CommandArgs.Count -ge 2 -and ($CommandArgs[0] -eq "-l" -or $CommandArgs[0] -eq "--label")) {
            Invoke-External "ctest" @("--test-dir", $BuildDir, "-C", $BuildConfig, "-L", $CommandArgs[1], "--output-on-failure")
        }
        elseif ($CommandArgs.Count -gt 0) {
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
