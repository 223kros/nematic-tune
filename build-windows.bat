@echo off
REM Compilacion local en Windows (alternativa a GitHub Actions).
REM Requisitos: Visual Studio 2022 (con "Desarrollo para el escritorio con C++") y CMake, ambos gratis.
cmake -S . -B build -A x64 || goto :error
cmake --build build --config Release --target NematicTune_VST3 --parallel || goto :error
echo.
echo Listo. El plugin esta en: build\NematicTune_artefacts\Release\VST3\Nematic Tune.vst3
pause
exit /b 0
:error
echo Fallo la compilacion. Copia el mensaje de error y mandamelo.
pause
exit /b 1
