@echo off
setlocal enabledelayedexpansion

:: Фиксируем рабочую директорию в папке скрипта
cd /d "%~dp0"

:: ===== CONFIGURATION =====
set "APP_FILE=app.py"
set "ICON_FILE=icon.ico"
set "APP_NAME=pulse-walker-bridge"
set "OUTPUT_DIR=dist\%APP_NAME%"
set "EXE_PATH=%OUTPUT_DIR%\%APP_NAME%.exe"
set "SHORTCUT_DEST=%CD%\%APP_NAME%.lnk"
:: =========================

echo =======================================
echo Building %APP_NAME% using PyInstaller
echo =======================================

if not exist "%APP_FILE%" (
    echo ERROR: %APP_FILE% not found!
    pause & exit /b 1
)

echo.
echo Installing dependencies...
pip install pyserial matplotlib pyinstaller
if errorlevel 1 (
    echo ERROR: pip install failed.
    pause & exit /b 1
)

echo.
echo Running PyInstaller...
if exist "%ICON_FILE%" (
    echo Using icon: %ICON_FILE%
    pyinstaller --clean --onedir --icon="%CD%\%ICON_FILE%" ^
        --hidden-import=matplotlib.backends.backend_tkagg ^
        --noconsole --name=%APP_NAME% %APP_FILE%
) else (
    echo Warning: %ICON_FILE% not found. Building without icon.
    pyinstaller --clean --onedir ^
        --hidden-import=matplotlib.backends.backend_tkagg ^
        --noconsole --name=%APP_NAME% %APP_FILE%
)

if errorlevel 1 (
    echo ERROR: PyInstaller build failed.
    pause & exit /b 1
)

if not exist "%EXE_PATH%" (
    echo ERROR: Executable not created at %EXE_PATH%
    pause & exit /b 1
)

echo.
echo Creating desktop shortcut...
powershell -ExecutionPolicy Bypass -command ^
"$WS = New-Object -ComObject WScript.Shell; ^
$SC = $WS.CreateShortcut('%SHORTCUT_DEST%'); ^
$SC.TargetPath = '%CD%\%EXE_PATH%'; ^
$SC.WorkingDirectory = '%CD%\%OUTPUT_DIR%'; ^
$SC.IconLocation = '%CD%\%EXE_PATH%, 0'; ^
$SC.Save()"

rmdir /s /q build 2>nul
del /q *.spec 2>nul

echo.
echo =======================================
echo Build completed successfully!
echo Executable: %CD%\%EXE_PATH%
echo Shortcut:   %SHORTCUT_DEST%
echo =======================================
pause