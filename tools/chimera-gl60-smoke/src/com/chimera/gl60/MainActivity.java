package com.chimera.gl60;

import android.app.Activity;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.Window;
import android.view.WindowManager;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Continuous-render GL ES smoke workload for the Chimera 1080p/60 verifier.
 *
 * Renders an actual rotating, colour-interpolated triangle over an animated
 * background every frame (RENDERMODE_CONTINUOUSLY), so the FPS measurement
 * exercises real draw calls and a real GLES ColorBuffer -> gfxstream ->
 * D3D11 post path, not just a screen clear.
 */
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
        // Interleaved: x, y, r, g, b per vertex.
        private static final float[] VERTICES = {
             0.0f,  0.6f,  1.0f, 0.0f, 0.0f,
            -0.6f, -0.5f,  0.0f, 1.0f, 0.0f,
             0.6f, -0.5f,  0.0f, 0.0f, 1.0f,
        };
        private static final int FLOATS_PER_VERTEX = 5;
        private static final int STRIDE = FLOATS_PER_VERTEX * 4;

        private static final String VERTEX_SHADER =
                "attribute vec2 aPos;\n" +
                "attribute vec3 aColor;\n" +
                "uniform float uAngle;\n" +
                "varying vec3 vColor;\n" +
                "void main() {\n" +
                "    float c = cos(uAngle);\n" +
                "    float s = sin(uAngle);\n" +
                "    vec2 p = vec2(aPos.x * c - aPos.y * s, aPos.x * s + aPos.y * c);\n" +
                "    gl_Position = vec4(p, 0.0, 1.0);\n" +
                "    vColor = aColor;\n" +
                "}\n";

        private static final String FRAGMENT_SHADER =
                "precision mediump float;\n" +
                "varying vec3 vColor;\n" +
                "void main() {\n" +
                "    gl_FragColor = vec4(vColor, 1.0);\n" +
                "}\n";

        private FloatBuffer vertexBuffer;
        private int program;
        private int aPosLoc;
        private int aColorLoc;
        private int uAngleLoc;
        private long frame;

        @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            GLES20.glDisable(GLES20.GL_DEPTH_TEST);

            vertexBuffer = ByteBuffer.allocateDirect(VERTICES.length * 4)
                    .order(ByteOrder.nativeOrder())
                    .asFloatBuffer();
            vertexBuffer.put(VERTICES).position(0);

            int vs = compileShader(GLES20.GL_VERTEX_SHADER, VERTEX_SHADER);
            int fs = compileShader(GLES20.GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
            program = GLES20.glCreateProgram();
            GLES20.glAttachShader(program, vs);
            GLES20.glAttachShader(program, fs);
            GLES20.glLinkProgram(program);
            int[] linked = new int[1];
            GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, linked, 0);
            if (linked[0] == 0) {
                throw new RuntimeException("program link failed: "
                        + GLES20.glGetProgramInfoLog(program));
            }
            aPosLoc = GLES20.glGetAttribLocation(program, "aPos");
            aColorLoc = GLES20.glGetAttribLocation(program, "aColor");
            uAngleLoc = GLES20.glGetUniformLocation(program, "uAngle");
        }

        @Override public void onSurfaceChanged(GL10 gl, int width, int height) {
            GLES20.glViewport(0, 0, width, height);
        }

        @Override public void onDrawFrame(GL10 gl) {
            frame++;
            float t = (frame % 360) / 360.0f;
            float pulse = 0.3f + 0.3f * (float) Math.sin(frame * 0.07f);
            GLES20.glClearColor(t, pulse, 1.0f - t, 1.0f);
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);

            GLES20.glUseProgram(program);
            GLES20.glUniform1f(uAngleLoc, frame * 0.05f);

            vertexBuffer.position(0);
            GLES20.glVertexAttribPointer(aPosLoc, 2, GLES20.GL_FLOAT, false, STRIDE, vertexBuffer);
            GLES20.glEnableVertexAttribArray(aPosLoc);

            vertexBuffer.position(2);
            GLES20.glVertexAttribPointer(aColorLoc, 3, GLES20.GL_FLOAT, false, STRIDE, vertexBuffer);
            GLES20.glEnableVertexAttribArray(aColorLoc);

            GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, 3);

            GLES20.glDisableVertexAttribArray(aPosLoc);
            GLES20.glDisableVertexAttribArray(aColorLoc);
        }

        private static int compileShader(int type, String source) {
            int shader = GLES20.glCreateShader(type);
            GLES20.glShaderSource(shader, source);
            GLES20.glCompileShader(shader);
            int[] compiled = new int[1];
            GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, compiled, 0);
            if (compiled[0] == 0) {
                String log = GLES20.glGetShaderInfoLog(shader);
                GLES20.glDeleteShader(shader);
                throw new RuntimeException("shader compile failed (type " + type + "): " + log);
            }
            return shader;
        }
    }
}
