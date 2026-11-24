package com.aetheria.gr;

import org.libsdl.app.SDLActivity;

public class AetheriaActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "main"
        };
    }

    @Override
    protected String getMainSharedObject() {
        return "libmain.so";
    }
}
