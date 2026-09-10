@echo off
setlocal EnableExtensions

set "REPO_ROOT=%~dp0.."
set "TOOL=%REPO_ROOT%\build\engine\tool\asset\RelWithDebInfo\KimPeanutAssetTool.exe"
set "ASSET_ROOT=%REPO_ROOT%\asset"
set "ARCHIVE_ROOT="
set "COMMAND="
set "SOURCE="

if "%~1"=="" goto usage_error
if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
if /I not "%~1"=="import" if /I not "%~1"=="reimport" (
    echo First argument must be import or reimport.
    goto usage_error
)
set "COMMAND=%~1"
shift
if "%~1"=="" (
    echo Missing source path.
    goto usage_error
)
set "SOURCE=%~1"
shift

:parse
if "%~1"=="" goto run
if /I "%~1"=="--asset-root" goto asset_root
if /I "%~1"=="--archive-root" goto archive_root
echo Unexpected argument: %~1
goto usage_error

:asset_root
if "%~2"=="" (
    echo Missing value for --asset-root
    goto usage_error
)
set "ASSET_ROOT=%~2"
shift
shift
goto parse

:archive_root
if "%~2"=="" (
    echo Missing value for --archive-root
    goto usage_error
)
set "ARCHIVE_ROOT=%~2"
shift
shift
goto parse

:run
if not exist "%TOOL%" (
    echo AssetTool was not found:
    echo   "%TOOL%"
    echo Build it with:
    echo   cmake --build build --config RelWithDebInfo --target KimPeanutAssetTool
    exit /b 2
)

echo %COMMAND% %SOURCE% with the AT1.6 performance profile...
if defined ARCHIVE_ROOT (
    "%TOOL%" %COMMAND% --source "%SOURCE%" --compression bc --bc-encoder reference --bc-quality balanced --asset-root "%ASSET_ROOT%" --archive-root "%ARCHIVE_ROOT%" --jobs 8 --memory-budget-mib 1024 --writer-queue-depth 2
) else (
    "%TOOL%" %COMMAND% --source "%SOURCE%" --compression bc --bc-encoder reference --bc-quality balanced --asset-root "%ASSET_ROOT%" --jobs 8 --memory-budget-mib 1024 --writer-queue-depth 2
)
exit /b %ERRORLEVEL%

:usage_error
echo.
set "USAGE_EXIT=2"
goto usage

:help
set "USAGE_EXIT=0"

:usage
echo KimPeanutEngine effective AssetTool import
echo.
echo Usage:
echo   tools\asset-import.bat import ^<asset-relative-source^> [options]
echo   tools\asset-import.bat reimport ^<asset-relative-source^> [options]
echo.
echo Options:
echo   --asset-root ^<path^>    Override the default repository asset directory
echo   --archive-root ^<path^>  Override the default asset\.archive directory
echo   --help                  Show this help
echo.
echo Profile:
echo   RelWithDebInfo, BC, ReferenceV1, Balanced, 8 workers, 1 GiB, queue depth 2
exit /b %USAGE_EXIT%
