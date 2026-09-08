@echo off
setlocal

set "ROOT=%~dp0"
set "ENGINE_EXE=%ROOT%build\Debug\KimPeanutEngine.exe"

if not exist "%ENGINE_EXE%" (
    echo Engine executable not found:
    echo   %ENGINE_EXE%
    echo Build the Debug target first.
    pause
    exit /b 1
)

"%ENGINE_EXE%" ^
    --graphics-api vulkan ^
    --startup-level level/performance_profile.level ^
    --agent-port 37373 ^
    %*

endlocal
