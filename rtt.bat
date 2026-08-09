@echo off
setlocal

cd /d "%~dp0"

if /i "%~1"=="/?" goto usage
if /i "%~1"=="--help" goto usage

set "OPENOCD_CONFIG=%~1"
if not defined OPENOCD_CONFIG goto invalid_config
if /i "%OPENOCD_CONFIG%"=="daplink.cfg" goto config_ok
if /i "%OPENOCD_CONFIG%"=="stlink.cfg" goto config_ok

:invalid_config
echo First argument must be daplink.cfg or stlink.cfg.
echo.
goto usage_error

:config_ok

set "RTT_PORT=%~2"
if not defined RTT_PORT set "RTT_PORT=9090"

set "RTT_CHANNEL=%~3"
if not defined RTT_CHANNEL set "RTT_CHANNEL=0"

set "INVALID_VALUE="
for /f "tokens=1,* delims=0123456789" %%A in ("x%RTT_PORT%") do (
    if not "%%A"=="x" set "INVALID_VALUE=1"
    if not "%%B"=="" set "INVALID_VALUE=1"
)
if defined INVALID_VALUE (
    echo Invalid RTT port: %RTT_PORT%
    pause
    exit /b 1
)

set "INVALID_VALUE="
for /f "tokens=1,* delims=0123456789" %%A in ("x%RTT_CHANNEL%") do (
    if not "%%A"=="x" set "INVALID_VALUE=1"
    if not "%%B"=="" set "INVALID_VALUE=1"
)
if defined INVALID_VALUE (
    echo Invalid RTT channel: %RTT_CHANNEL%
    pause
    exit /b 1
)

where openocd >nul 2>nul
if errorlevel 1 (
    echo OpenOCD was not found in PATH.
    pause
    exit /b 1
)

if not exist "%OPENOCD_CONFIG%" (
    echo OpenOCD config was not found: %OPENOCD_CONFIG%
    pause
    exit /b 1
)

start "RTT Channel %RTT_CHANNEL%" powershell -NoProfile -Command ^
    "$client = $null; $deadline = [DateTime]::UtcNow.AddSeconds(10); while ($null -eq $client) { try { $client = [Net.Sockets.TcpClient]::new('127.0.0.1', %RTT_PORT%) } catch { if ([DateTime]::UtcNow -ge $deadline) { throw 'RTT server connection timed out.' }; Start-Sleep -Milliseconds 250 } }; $stream = $client.GetStream(); $output = [Console]::OpenStandardOutput(); $buffer = [byte[]]::new(1024); try { while (($count = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) { $output.Write($buffer, 0, $count); $output.Flush() } } finally { $client.Dispose() }"

openocd -f "%OPENOCD_CONFIG%" ^
    -c "init" ^
    -c "rtt setup 0x24000000 0x50000 {SEGGER RTT}" ^
    -c "rtt polling_interval 10" ^
    -c "rtt start" ^
    -c "rtt server start %RTT_PORT% %RTT_CHANNEL%"

set "RTT_EXIT_CODE=%ERRORLEVEL%"
if not "%RTT_EXIT_CODE%"=="0" pause
endlocal & exit /b %RTT_EXIT_CODE%

:usage
echo Usage: rtt.bat ^<daplink.cfg^|stlink.cfg^> [port] [channel]
echo Defaults: port 9090, channel 0
exit /b 0

:usage_error
echo Usage: rtt.bat ^<daplink.cfg^|stlink.cfg^> [port] [channel]
exit /b 1
