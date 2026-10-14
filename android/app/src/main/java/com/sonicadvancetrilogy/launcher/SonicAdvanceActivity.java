package com.sonicadvancetrilogy.launcher;

import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Copia al directorio escribible de la app los recursos versionados que se
 * empaquetan desde dist/. La copia ocurre ANTES de iniciar SDL_main para que
 * el launcher encuentre las ROMs y los PNG al crear App.
 *
 * Solo crea archivos ausentes; nunca sobrescribe ROMs, partidas, configuración
 * ni recursos que el usuario ya tenga en su directorio de datos.
 */
public final class SonicAdvanceActivity extends SDLActivity {
    private static final String TAG = "SonicAdvanceTrilogy";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        bootstrapBundledContent();
        super.onCreate(savedInstanceState);
    }

    private void bootstrapBundledContent() {
        File appBase = getExternalFilesDir(null);
        if (appBase == null) {
            appBase = getFilesDir();
        }
        if (appBase == null) {
            Log.e(TAG, "No se pudo localizar el directorio de datos para copiar ROMs/assets");
            return;
        }

        copyAssetTree("main/roms", new File(appBase, "main/roms"));
        copyAssetTree("assets", new File(appBase, "assets"));
        Log.i(TAG, "Contenido incluido en el APK preparado en: " + appBase.getAbsolutePath());
    }

    /** Copia recursivamente desde assets del APK, preservando rutas relativas. */
    private void copyAssetTree(String assetPath, File target) {
        final String[] children;
        try {
            children = getAssets().list(assetPath);
        } catch (IOException e) {
            Log.w(TAG, "No se pudo listar el contenido empaquetado " + assetPath, e);
            return;
        }

        if (children != null && children.length > 0) {
            if (!target.exists() && !target.mkdirs() && !target.isDirectory()) {
                Log.e(TAG, "No se pudo crear el directorio " + target.getAbsolutePath());
                return;
            }
            for (String child : children) {
                copyAssetTree(assetPath + "/" + child, new File(target, child));
            }
            return;
        }

        // Un recurso hoja tiene una lista vacía; los ficheros existentes se respetan.
        if (target.exists()) {
            return;
        }
        File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs() && !parent.isDirectory()) {
            Log.e(TAG, "No se pudo crear el directorio " + parent.getAbsolutePath());
            return;
        }

        File temporary = new File(target.getAbsolutePath() + ".tmp");
        try (InputStream input = getAssets().open(assetPath);
             OutputStream output = new FileOutputStream(temporary)) {
            byte[] buffer = new byte[32 * 1024];
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
            }
            output.flush();
        } catch (IOException e) {
            // Puede ser un directorio vacío en assets; no bloqueamos el inicio del juego.
            // Los errores de recursos no encontrados quedan visibles en logcat.
            if (temporary.exists()) {
                //noinspection ResultOfMethodCallIgnored
                temporary.delete();
            }
            Log.w(TAG, "No se pudo copiar el recurso empaquetado " + assetPath, e);
            return;
        }

        if (!temporary.renameTo(target)) {
            //noinspection ResultOfMethodCallIgnored
            temporary.delete();
            Log.e(TAG, "No se pudo instalar el recurso " + target.getAbsolutePath());
        } else {
            Log.d(TAG, "Instalado desde el APK: " + assetPath);
        }
    }
}
