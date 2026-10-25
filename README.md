# NEMATIC TUNE — Autotune VST3 para voces

Plugin de corrección de afinación para FL Studio (Windows), con dos modos y una interfaz synthwave violeta/neón.

| Modo | Para qué | Cómo suena |
|---|---|---|
| **BOOM BAP** | Rap, voz hablada/cantada natural | Corrige lento y solo una parte (retune 110 ms, 70 %, y deja intacta la desafinación de ±25 cents). Se oye como una toma a capela, pero cantada "justa". |
| **HARD** | Trap, melodías, efecto autotune | Retune instantáneo al 100 %: la voz salta de nota en nota, bien robótica. |

Controles: **KEY** y **SCALE** (el plugin solo corrige a las notas de esa escala), **RETUNE SPEED**, **AMOUNT**, **MIX** y **OUTPUT**. Al tocar un modo, los mandos se ponen en sus valores; después los podés ajustar a gusto. El visor muestra la nota que cantás (cian) y la nota ya corregida (rosa).

---

## Cómo obtener el archivo .vst3 (sin instalar nada)

El `.vst3` hay que compilarlo. Lo más fácil es que GitHub lo haga por vos (es gratis, tarda unos 5–10 minutos):

1. Creá una cuenta en https://github.com (si no tenés).
2. Arriba a la derecha: **+ → New repository**. Nombre: `NematicTune`. Podés dejarlo **Private**. Tocá **Create repository**.
3. En la página del repo tocá **uploading an existing file**. Descomprimí este ZIP y **arrastrá TODO el contenido** (las carpetas `Source`, `installer`, `tests`, `.github` y los archivos sueltos). Importante: tiene que subirse también la carpeta `.github`.
4. Abajo tocá **Commit changes**.
5. Andá a la pestaña **Actions**. Vas a ver "Build Nematic Tune (Windows VST3)" corriendo. Esperá a que aparezca el tilde verde.
6. Entrá a esa ejecución, bajá hasta **Artifacts** y descargá **Nematic-Tune-VST3-Windows**.

> Si la ejecución sale en rojo, abrila, copiá el mensaje de error y mandámelo: lo arreglo.

## Instalarlo

1. Descomprimí el ZIP descargado.
2. Clic derecho en **INSTALAR-Nematic-Tune.bat → Ejecutar como administrador** (copia el plugin a `C:\Program Files\Common Files\VST3`).
3. En FL Studio: **Options → Manage plugins → Find installed plugins** (Start scan).
4. Abrí un canal del **Mixer**, en un slot de efecto elegí **Nematic Tune** (aparece en "Installed → Effects").

## Cómo usarlo para grabar

- Poné el plugin en el insert donde entra tu micrófono, **antes** de reverb, delay y compresión.
- Elegí la **KEY y SCALE** del beat (si no la sabés, dejá la escala en *Chromatic*).
- Para sonar natural: **BOOM BAP**. Para el efecto: **HARD**.
- **Latencia:** el plugin tiene ~32 ms de retardo (FL lo compensa solo al mezclar). Si grabás escuchándote por el plugin vas a notar el retardo; lo mejor es monitorear la voz seca (directo de la placa) y escuchar el efecto después de grabar.
- Rango de voz detectado: de ~80 Hz a ~900 Hz.

## Compilar en tu propia PC (opcional)

Necesitás Visual Studio 2022 (con "Desarrollo para el escritorio con C++") y CMake. Ejecutá `build-windows.bat`.

## Estructura

- `Source/AutotuneEngine.h` – motor: detector de pitch (YIN), cuantizador a escala y cambio de tono PSOLA (mantiene el timbre/formantes de la voz).
- `Source/PluginProcessor.*`, `PluginEditor.*`, `NeonLookAndFeel.h`, `PitchDisplay.h` – plugin JUCE e interfaz.
- `tests/` – banco de pruebas offline del motor (`engine_test.cpp` + `analyze.py`).

NEMATIC © 2026
