@echo off
setlocal

set "ROOT=%~dp0"
set "UVISION=C:\soft\Keil_v5\UV4\uVision.com"
set "PROJECT=%ROOT%MDK-ARM\Project.uvprojx"
set "TARGET_NAME=py32f002b_qmi8656_jacdac_clean_hybrid_phy_build"
set "HEX=%ROOT%MDK-ARM\Output\%TARGET_NAME%.hex"
set "BUILD_LOG=%TEMP%\%TARGET_NAME%_build.log"
set "PACK=%ROOT%..\\PY32F0xx_Keil_DFP_V1.2.19\\Puya.PY32F0xx_DFP.1.2.19.pack"
set "TARGET=py32f002bx5"
set "PROBE_UID=006000133900001456314D43"
set "FREQUENCY=1M"

echo [1/2] Building %TARGET_NAME%

if not exist "%UVISION%" (
  echo ERROR: Keil command-line tool not found:
  echo   %UVISION%
  goto fail
)

if not exist "%PROJECT%" (
  echo ERROR: Project file not found:
  echo   %PROJECT%
  goto fail
)

if exist "%BUILD_LOG%" del /f /q "%BUILD_LOG%" >nul 2>nul

"%UVISION%" -r "%PROJECT%" -j0 -o "%BUILD_LOG%"

findstr /C:"0 Error(s)" "%BUILD_LOG%" >nul 2>nul
if errorlevel 1 (
  echo ERROR: Build failed. Log:
  echo   %BUILD_LOG%
  goto fail
)

if not exist "%HEX%" (
  echo ERROR: Build reported success but HEX was not generated:
  echo   %HEX%
  goto fail
)

echo Build succeeded:
echo   %HEX%
echo.
echo [2/2] Flashing %TARGET%

if not exist "%PACK%" (
  echo ERROR: Keil DFP pack not found:
  echo   %PACK%
  goto fail
)

set "PYTHON="
for %%P in ("%LOCALAPPDATA%\Programs\Python\Python312\python.exe" "%USERPROFILE%\.platformio\penv\Scripts\python.exe" "py" "python" "C:\soft\Keil_v5\ARM\VHT\python.exe") do (
  if not defined PYTHON (
    call "%%~P" -m pyocd --version >nul 2>nul
    if not errorlevel 1 set "PYTHON=%%~P"
  )
)

if not defined PYTHON (
  echo ERROR: No Python runtime with pyOCD was found.
  goto fail
)

call "%PYTHON%" -m pyocd load --pack "%PACK%" -t "%TARGET%" -u "%PROBE_UID%" -f "%FREQUENCY%" -M halt -e sector "%HEX%"
if errorlevel 1 (
  echo ERROR: pyOCD flash failed.
  goto fail
)

echo.
echo Build and flash succeeded.
pause
exit /b 0

:fail
echo.
echo Build and flash failed.
pause
exit /b 1

