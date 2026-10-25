@echo off
chcp 65001 >nul
title Instalar Nematic Tune (VST3)

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo  Necesito permisos de administrador.
    echo  Cerra esta ventana, hace clic DERECHO en este archivo y elegi "Ejecutar como administrador".
    echo.
    pause
    exit /b 1
)

set "DEST=%CommonProgramFiles%\VST3"
if not exist "%DEST%" mkdir "%DEST%"

if not exist "%~dp0Nematic Tune.vst3" (
    echo  No encuentro la carpeta "Nematic Tune.vst3" junto a este archivo.
    echo  Descomprimi el ZIP completo antes de ejecutar el instalador.
    pause
    exit /b 1
)

xcopy /E /I /Y "%~dp0Nematic Tune.vst3" "%DEST%\Nematic Tune.vst3" >nul
if %errorlevel% neq 0 (
    echo  Algo fallo al copiar. Cerra FL Studio y proba de nuevo.
    pause
    exit /b 1
)

echo.
echo  Listo. Nematic Tune quedo instalado en:
echo  %DEST%\Nematic Tune.vst3
echo.
echo  Ahora abri FL Studio ^> Options ^> Manage plugins ^> "Find installed plugins" (Start scan).
echo.
pause
