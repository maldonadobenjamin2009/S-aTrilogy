# Compilar el APK Android

Esta carpeta contiene el proyecto Android Studio (Gradle + NDK + CMake) para el launcher C/SDL2/mGBA.
La actividad usa SDL2 oficial, y `src/main_sdl.c` dibuja controles táctiles mientras hay una partida abierta.

## Requisitos

- Android Studio reciente, JDK 17, Android SDK Platform 35, NDK y CMake 3.22.1 instalados desde SDK Manager.
- Conexión a Internet en el primer sync: `app/build.gradle` descarga SDL2 2.30.9 del repositorio oficial de SDL. Si prefieres preparar el árbol manualmente, extrae el contenido de SDL2 2.30.9 en `android/SDL2/` (debe quedar `android/SDL2/CMakeLists.txt` y `android/SDL2/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java`). No es una copia modificada de SDL.

## Compilar

1. Abre la carpeta `android/` en Android Studio.
2. Espera a que Gradle sincronice y descargue SDL2 y las dependencias Android.
3. En **Build > Build Bundle(s) / APK(s) > Build APK(s)**, genera el APK debug.
4. Android Studio mostrará la ruta del APK en `android/app/build/outputs/apk/debug/`. También puede compilarse con `gradle :app:assembleDebug` desde esta carpeta una vez instalado Gradle/Android SDK.

El proyecto compila las fuentes del launcher y mGBA desde el `CMakeLists.txt` de la raíz; no hace falta mantener una segunda copia del núcleo.

## ROMs y assets incluidos en el APK

La compilación incorpora automáticamente el contenido de `dist/main/roms/` y `dist/assets/` dentro de los assets del APK. Al iniciar, `SonicAdvanceActivity` copia los archivos empaquetados que falten al directorio de datos de la app antes de arrancar SDL/mGBA. Así, la primera ejecución encuentra las ROMs y las imágenes sin tener que copiarlas manualmente. La copia no sobrescribe archivos existentes, por lo que se conservan tus partidas, configuraciones y recursos personalizados.

La tarea `verifyBundledGameData` hace fallar la compilación si el checkout no contiene al menos una ROM (`.gba`, `.gbc` o `.gb`) y una imagen compatible en `dist/assets/`. Si el workflow falla en esa comprobación, revisa que esos archivos estén realmente rastreados en Git y disponibles en la rama que se compila.

Los archivos incluidos se copian al directorio privado de la aplicación. También puedes añadir o reemplazar tus propias ROMs en:

`Android/data/com.sonicadvancetrilogy.launcher/files/main/roms/`

Los nombres detectados por defecto son `sonicadvance.gba`, `sonicadvance2.gba` y `sonicadvance3.gba`. Para copiar por ADB, por ejemplo:

```sh
adb push sonicadvance.gba /sdcard/Android/data/com.sonicadvancetrilogy.launcher/files/main/roms/
```

Los guardados de batería, estados y configuración quedan bajo `main/saves/` y `main/config.txt` dentro del mismo directorio privado. Android puede restringir el acceso a `Android/data` desde algunos gestores de archivos; ADB es una alternativa.

## Controles táctiles

En horizontal y durante el juego aparecen controles semitransparentes:

- Cruceta a la izquierda.
- A y B a la derecha.
- L y R en las esquinas superiores.
- Pausa centrada arriba.
- SELECT y START centrados abajo.

Los controles táctiles emiten eventos de mando SDL y pueden coexistir con un mando Bluetooth. La pantalla de pausa se controla tocando la interfaz como si fuera un ratón.

## Limitaciones conocidas

- Este entorno de edición no incluye Android SDK/NDK ni conexión de descarga, por lo que el APK no se ha podido compilar aquí. La compilación debe verificarse en Android Studio.
- No se incluye todavía un selector Android de archivos; las ROMs presentes en `dist/main/roms/` se empaquetan con el APK automáticamente.
- El modo de pantalla dividida queda disponible si conectas un mando compatible; los controles táctiles son para J1.
