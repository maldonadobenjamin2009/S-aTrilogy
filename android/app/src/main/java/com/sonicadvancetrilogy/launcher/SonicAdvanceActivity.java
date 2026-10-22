package com.sonicadvancetrilogy.launcher;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Instala ROMs y assets del APK en Android/obb/<paquete>, separándolos de la
 * carpeta Android/data. Las partidas y la configuración siguen en Android/data.
 * Los archivos que ya existen en OBB no se sobrescriben.
 */
public final class SonicAdvanceActivity extends SDLActivity {
    private static final String TAG = "SonicAdvanceTrilogy";
    private static final String ENV_RESOURCE_BASE = "SAT_RESOURCE_BASE";
    private static final String ENV_USER_DATA_BASE = "SAT_USER_DATA_BASE";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        bootstrapBundledContent();
        super.onCreate(savedInstanceState);
    }

    private void bootstrapBundledContent() {
        File legacyBase = getExternalFilesDir(null); // Android/data/<paquete>/files
        File obbBase = getObbDir();                  // Android/obb/<paquete>
        File appBase = null;

        if (ensureWritableDirectory(obbBase)) {
            appBase = obbBase;
            Log.i(TAG, "Usando Android/obb para ROMs y assets: " + appBase.getAbsolutePath());
        } else if (ensureWritableDirectory(legacyBase)) {
            appBase = legacyBase;
            Log.w(TAG, "Android/obb no está disponible; se usa Android/data: " + appBase.getAbsolutePath());
        } else {
            File internal = getFilesDir();
            if (ensureWritableDirectory(internal)) {
                appBase = internal;
                Log.w(TAG, "Almacenamiento externo no disponible; se usa almacenamiento interno: " + appBase.getAbsolutePath());
            }
        }

        if (appBase == null) {
            Log.e(TAG, "No se encontró un directorio escribible para instalar ROMs/assets");
            return;
        }

        File targetRoms = new File(appBase, "main/roms");
        File targetAssets = new File(appBase, "assets");

        // Migrar primero ROMs del directorio anterior para preservar las del usuario.
        if (legacyBase != null && !sameDirectory(legacyBase, appBase)) {
            copyMissingTree(new File(legacyBase, "main/roms"), targetRoms);
        }

        // Los recursos incluidos en esta versión rellenan los archivos ausentes.
        copyAssetTree("main/roms", targetRoms);
        copyAssetTree("assets", targetAssets);

        // Restaura assets personalizados que no venían en el APK anterior; no reemplaza
        // las carátulas e iconos incluidos en el paquete actual.
        if (legacyBase != null && !sameDirectory(legacyBase, appBase)) {
            copyMissingTree(new File(legacyBase, "assets"), targetAssets);
        }

        File userDataBase = ensureWritableDirectory(legacyBase) ? legacyBase : getFilesDir();
        boolean resourcePublished = publishNativePath(ENV_RESOURCE_BASE, appBase);
        boolean userDataPublished = publishNativePath(ENV_USER_DATA_BASE, userDataBase);
        if (!resourcePublished && !sameDirectory(appBase, legacyBase)) {
            Log.e(TAG, "No se pudo publicar SAT_RESOURCE_BASE; el código nativo podría usar la ruta anterior");
        }
        if (!userDataPublished) {
            Log.w(TAG, "No se pudo publicar SAT_USER_DATA_BASE; partidas/configuración podrían usar la ruta predeterminada");
        }

        Log.i(TAG, "Recursos: " + appBase.getAbsolutePath());
        Log.i(TAG, "Partidas/configuración: " + userDataBase.getAbsolutePath());
    }

    private boolean publishNativePath(String name, File directory) {
        if (directory == null) return false;
        try {
            Os.setenv(name, directory.getAbsolutePath(), true);
            return true;
        } catch (ErrnoException e) {
            Log.e(TAG, "No se pudo establecer " + name + "=" + directory.getAbsolutePath(), e);
            return false;
        }
    }

    private boolean ensureWritableDirectory(File directory) {
        if (directory == null) return false;
        if (!directory.isDirectory() && !directory.mkdirs() && !directory.isDirectory()) return false;
        File probe = new File(directory, ".sat-write-check");
        try (FileOutputStream output = new FileOutputStream(probe)) {
            output.write(1);
            output.flush();
        } catch (IOException e) {
            Log.w(TAG, "No se puede escribir en " + directory.getAbsolutePath(), e);
            return false;
        }
        if (probe.exists() && !probe.delete()) {
            Log.w(TAG, "No se pudo borrar el archivo de prueba " + probe.getAbsolutePath());
        }
        return true;
    }

    private boolean sameDirectory(File a, File b) {
        if (a == null || b == null) return false;
        try {
            return a.getCanonicalFile().equals(b.getCanonicalFile());
        } catch (IOException e) {
            return a.getAbsolutePath().equals(b.getAbsolutePath());
        }
    }

    /** Copia al destino únicamente archivos que todavía no existen. */
    private void copyMissingTree(File source, File target) {
        if (source == null || !source.exists()) return;
        if (source.isDirectory()) {
            if (!target.exists() && !target.mkdirs() && !target.isDirectory()) {
                Log.w(TAG, "No se pudo crear " + target.getAbsolutePath());
                return;
            }
            File[] children = source.listFiles();
            if (children == null) {
                Log.w(TAG, "No se pudo enumerar " + source.getAbsolutePath());
                return;
            }
            for (File child : children) copyMissingTree(child, new File(target, child.getName()));
            return;
        }
        if (!source.isFile() || target.exists()) return;
        File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs() && !parent.isDirectory()) {
            Log.w(TAG, "No se pudo crear " + parent.getAbsolutePath());
            return;
        }

        File temporary = new File(target.getAbsolutePath() + ".tmp");
        try (InputStream input = new FileInputStream(source);
             OutputStream output = new FileOutputStream(temporary)) {
            byte[] buffer = new byte[32 * 1024];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            output.flush();
        } catch (IOException e) {
            if (temporary.exists()) {
                //noinspection ResultOfMethodCallIgnored
                temporary.delete();
            }
            Log.w(TAG, "No se pudo migrar " + source.getAbsolutePath(), e);
            return;
        }
        if (!temporary.renameTo(target)) {
            //noinspection ResultOfMethodCallIgnored
            temporary.delete();
            Log.w(TAG, "No se pudo instalar " + target.getAbsolutePath());
        }
    }

    /** Copia recursivamente desde los assets del APK, preservando rutas relativas. */
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
            for (String child : children) copyAssetTree(assetPath + "/" + child, new File(target, child));
            return;
        }

        if (target.exists()) return;
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
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            output.flush();
        } catch (IOException e) {
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
