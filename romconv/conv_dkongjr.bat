@echo off
python ./pyconv/conv_dkongjr.py
if errorlevel 1 goto :error
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
