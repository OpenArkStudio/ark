@echo off
setlocal
if "%~3"=="" goto usage
if not "%~5"=="" goto usage
set "binary="
if "%~1"=="Release" set "binary=bin\app.exe"
if "%~1"=="Debug" set "binary=bin\app_d.exe"
if not defined binary goto usage
echo(%~2| findstr /r /x "[a-zA-Z0-9_-][a-zA-Z0-9_-]*" >nul || goto usage
set "runtime=%~dp0"
if not "%~4"=="" set "runtime=%~4"
pushd "%runtime%" || exit /b 1
if not exist "%binary%" goto missing
if not exist "conf\%~2.app.toml" goto missing
"%binary%" "--busid=%~3" "--name=%~2" "--app_conf=conf/%~2.app.toml"
set "result=%errorlevel%"
popd
exit /b %result%
:missing
echo Missing executable or configuration in "%CD%". 1>&2
popd
exit /b 1
:usage
echo Usage: %~nx0 ^<Release^|Debug^> ^<name^> ^<busid^> [runtime-directory] 1>&2
exit /b 2
