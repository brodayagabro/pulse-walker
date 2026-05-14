@echo off
setlocal enabledelayedexpansion

:: ===== CONFIGURATION =====
set "APP_FILE=app.py"
set "ICON_FILE=icon.ico"
set "APP_NAME=pulse-walker-bridge"
set "OUTPUT_DIR=dist\%APP_NAME%"
set "EXE_PATH=%OUTPUT_DIR%\%APP_NAME%.exe"

:: Where to create the shortcut? (choose one)
set "SHORTCUT_DEST=%CD%\%APP_NAME%.lnk"          :: current folder
:: set "SHORTCUT_DEST=%USERPROFILE%\Desktop\%APP_NAME%.lnk"   :: desktop
:: =========================

echo =======================================
echo Building %APP_NAME% using PyInstaller
echo =======================================

:: 1. Check if app.py exists
if not exist "%APP_FILE%" (
    echo ERROR: %APP_FILE% not found!
    pause
    exit /b 1
)

:: 2. Check icon file
set "ICON_ARG="
if exist "%ICON_FILE%" (
    set "ICON_ARG=--icon=%ICON_FILE%"
    echo Icon found: %ICON_FILE%
) else (
    echo Warning: %ICON_FILE% not found, building without icon.
)

:: 3. Install dependencies
echo.
echo Installing/updating dependencies...
pip install pyserial matplotlib pyinstaller
if errorlevel 1 (
    echo ERROR: Failed to install dependencies.
    pause
    exit /b 1
)

:: 4. Run PyInstaller
echo.
echo Running PyInstaller...
pyinstaller --onedir %ICON_ARG% ^
    --hidden-import=matplotlib.backends.backend_tkagg ^
    --noconsole --name=%APP_NAME% %APP_FILE%

if errorlevel 1 (
    echo ERROR: PyInstaller build failed.
    pause
    exit /b 1
)

:: 5. Verify that the EXE was created
if not exist "%EXE_PATH%" (
    echo ERROR: Executable not found at %EXE_PATH%
    pause
    exit /b 1
)

:: 6. Create shortcut using PowerShell
echo.
echo Creating shortcut %APP_NAME%.lnk...
powershell -command ^
"$WS = New-Object -ComObject WScript.Shell; ^
$SC = $WS.CreateShortcut('%SHORTCUT_DEST%'); ^
$SC.TargetPath = '%CD%\%EXE_PATH%'; ^
$SC.WorkingDirectory = '%CD%\%OUTPUT_DIR%'; ^
$SC.IconLocation = '%CD%\%EXE_PATH%'; ^
$SC.Save()"

if errorlevel 1 (
    echo Failed to create shortcut via PowerShell.
) else (
    echo Shortcut created: %SHORTCUT_DEST%
)

:: 7. Clean up temporary files (optional)
rmdir /s /q build 2>nul
del /q *.spec 2>nul

echo.
echo =======================================
echo Build completed successfully!
echo Executable: %CD%\%EXE_PATH%
echo Shortcut: %SHORTCUT_DEST%
echo =======================================
pause