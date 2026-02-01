@echo off
set firstGuess=%1
if "%firstGuess%" == "" set firstGuess=raise
set args=
:loop
    for /f %%i in ('.\wordler.exe --no-verbose --init^=%firstGuess% %args%') do set guess=%%i
    if not %ERRORLEVEL% == 0 goto exitError
    echo Guess: %guess%
    set hint=
    set /p hint=Hint:  
    if "%hint%" == "" goto done
    set args=%args% %guess% %hint%
    goto loop
:done
exit 0
:exitError
set err=%ERRORLEVEL%
echo ERROR %err%
pause
exit %err%
