@echo off
python ./conv_all.py
if errorlevel 1 goto :error
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
