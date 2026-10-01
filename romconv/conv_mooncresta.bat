@echo off
python ./pyconv/conv_mooncresta.py
if errorlevel 1 goto :error
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
