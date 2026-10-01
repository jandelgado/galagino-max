@echo off
python ./pyconv/conv_fantasy.py
if errorlevel 1 goto :error
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
