package com.chimera.gl60;

import android.app.Activity;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.Window;
import android.view.WindowManager;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public final class MainActivity extends Activity {
    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(
                WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);

        GLSurfaceView view = new GLSurfaceView(this);
        view.setEGLContextClientVersion(2);
        view.setPreserveEGLContextOnPause(true);
        view.setRenderer(new Renderer());
        view.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        setContentView(view);
    }

    private static final class Renderer implements GLSurfaceView.Renderer {
        private long frame;

        @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            GLES20.glDisable(GLES20.GL_DEPTH_TEST);
        }

        @Override public void onSurfaceChanged(GL10 gl, int width, int height) {
            GLES20.glViewport(0, 0, width, height);
        }

        @Override public void onDrawFrame(GL10 gl) {
            frame++;
            float t = (frame % 360) / 360.0f;
            float pulse = 0.3f + 0.3f * (float)Math.sin(frame * 0.07f);
            GLES20.glClearColor(t, pulse, 1.0f - t, 1.0f);
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
        }
    }
}
