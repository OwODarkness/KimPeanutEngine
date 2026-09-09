@echo off
setlocal

set "ROOT=%~dp0.."
set "PYTHON=%ROOT%\.venv\Scripts\python.exe"

if not exist "%PYTHON%" (
    echo Local MCP Python environment not found. Run: python -m venv .venv 1>&2
    echo Then install: .venv\Scripts\python.exe -m pip install -r mcp\requirements.txt 1>&2
    exit /b 1
)

"%PYTHON%" "%ROOT%\mcp\server.py" %*

endlocal
