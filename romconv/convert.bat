@echo off
cd /d "%~dp0"
uv run convert.py %*
if errorlevel 1 goto :error
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
