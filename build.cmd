@echo off
mkdir E:\001\AIC-BarDisplay\build\.tmp
set TMPDIR=E:\001\AIC-BarDisplay\build\.tmp
set TMP=E:\001\AIC-BarDisplay\build\.tmp
set TEMP=E:\001\AIC-BarDisplay\build\.tmp
cd /d "%~dp0"
set PYTHONDONTWRITEBYTECODE=1
"%~dp0tools\python\python3.exe" "%~dp0build.py" %*
exit /b %errorlevel%
