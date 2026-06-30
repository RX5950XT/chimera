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
 * Default (light) mode renders a rotating, colour-interpolated triangle over an
 * animated background every frame (RENDERMODE_CONTINUOUSLY) — the canonical
 * "trivial per-frame fill" 60 FPS gate.
 *
 * Heavy mode (launch with --ei heavyIters N, N>0) instead draws a full-screen
 * plasma quad whose fragment shader runs an N-iteration sin/cos loop per pixel.
 * At 1080p that is ~2M*N ALU ops/frame, a faithful fragment-bound load. This is
 * the item-2 probe: it characterises how much per-frame GLES fill the
 * gpu-direct post path absorbs before the *guest GLES backend* (host SwiftShader
 * in headless custom runtime) becomes the limiter — NOT a claim about
 * HW-accelerated games, which render via guest Vulkan -> NVIDIA.
 */
public final class MainActivity extends Activity {
    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(
                WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);

        int heavyIters = getIntent().getIntExtra("heavyIters", 0);

        GLSurfaceView view = new GLSurfaceView(this);
        view.setEGLContextClientVersion(2);
        view.setPreserveEGLContextOnPause(true);
        view.setRenderer(new Renderer(heavyIters));
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

        // Full-screen quad (two triangles) in clip space for the heavy plasma pass.
        private static final float[] QUAD = {
            -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
        };

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

        private static final String QUAD_VERTEX_SHADER =
                "attribute vec2 aPos;\n" +
                "void main() {\n" +
                "    gl_Position = vec4(aPos, 0.0, 1.0);\n" +
                "}\n";

        // GLSL ES 1.00 requires constant loop bounds, so the iteration count is
        // spliced in as a compile-time constant (__ITERS__) before compilation.
        // The accumulator depends on gl_FragCoord and uTime so the loop cannot be
        // constant-folded away.
        private static final String QUAD_FRAGMENT_SHADER_TEMPLATE =
                "precision mediump float;\n" +
                "uniform float uTime;\n" +
                "uniform vec2 uRes;\n" +
                "void main() {\n" +
                "    vec2 uv = gl_FragCoord.xy / uRes;\n" +
                "    float v = 0.0;\n" +
                "    for (int i = 0; i < __ITERS__; i++) {\n" +
                "        float fi = float(i);\n" +
                "        v += sin(uv.x * 10.0 + uTime + fi) * cos(uv.y * 10.0 - uTime + fi * 0.5);\n" +
                "    }\n" +
                "    v = v / float(__ITERS__);\n" +
                "    gl_FragColor = vec4(0.5 + 0.5 * sin(v + uTime),\n" +
                "                        0.5 + 0.5 * cos(v * 1.3 - uTime),\n" +
                "                        0.5 + 0.5 * sin(v * 0.7 + uTime * 0.5), 1.0);\n" +
                "}\n";

        private final int heavyIters;

        private FloatBuffer vertexBuffer;
        private FloatBuffer quadBuffer;
        private int program;
        private int aPosLoc;
        private int aColorLoc;
        private int uAngleLoc;
        private int quadProgram;
        private int quadPosLoc;
        private int quadTimeLoc;
        private int quadResLoc;
        private int viewW;
        private int viewH;
        private long frame;

        Renderer(int heavyIters) {
            this.heavyIters = heavyIters;
        }

        @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            GLES20.glDisable(GLES20.GL_DEPTH_TEST);

            vertexBuffer = ByteBuffer.allocateDirect(VERTICES.length * 4)
                    .order(ByteOrder.nativeOrder())
                    .asFloatBuffer();
            vertexBuffer.put(VERTICES).position(0);

            int vs = compileShader(GLES20.GL_VERTEX_SHADER, VERTEX_SHADER);
            int fs = compileShader(GLES20.GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
            program = linkProgram(vs, fs);
            aPosLoc = GLES20.glGetAttribLocation(program, "aPos");
            aColorLoc = GLES20.glGetAttribLocation(program, "aColor");
            uAngleLoc = GLES20.glGetUniformLocation(program, "uAngle");

            if (heavyIters > 0) {
                quadBuffer = ByteBuffer.allocateDirect(QUAD.length * 4)
                        .order(ByteOrder.nativeOrder())
                        .asFloatBuffer();
                quadBuffer.put(QUAD).position(0);

                String heavySrc = QUAD_FRAGMENT_SHADER_TEMPLATE
                        .replace("__ITERS__", Integer.toString(heavyIters));
                int qvs = compileShader(GLES20.GL_VERTEX_SHADER, QUAD_VERTEX_SHADER);
                int qfs = compileShader(GLES20.GL_FRAGMENT_SHADER, heavySrc);
                quadProgram = linkProgram(qvs, qfs);
                quadPosLoc = GLES20.glGetAttribLocation(quadProgram, "aPos");
                quadTimeLoc = GLES20.glGetUniformLocation(quadProgram, "uTime");
                quadResLoc = GLES20.glGetUniformLocation(quadProgram, "uRes");
            }
        }

        @Override public void onSurfaceChanged(GL10 gl, int width, int height) {
            viewW = width;
            viewH = height;
            GLES20.glViewport(0, 0, width, height);
        }

        @Override public void onDrawFrame(GL10 gl) {
            frame++;

            if (heavyIters > 0) {
                drawHeavyQuad();
                return;
            }

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

        private void drawHeavyQuad() {
            GLES20.glUseProgram(quadProgram);
            GLES20.glUniform1f(quadTimeLoc, frame * 0.05f);
            GLES20.glUniform2f(quadResLoc, (float) viewW, (float) viewH);

            quadBuffer.position(0);
            GLES20.glVertexAttribPointer(quadPosLoc, 2, GLES20.GL_FLOAT, false, 0, quadBuffer);
            GLES20.glEnableVertexAttribArray(quadPosLoc);
            GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, 6);
            GLES20.glDisableVertexAttribArray(quadPosLoc);
        }

        private static int linkProgram(int vs, int fs) {
            int prog = GLES20.glCreateProgram();
            GLES20.glAttachShader(prog, vs);
            GLES20.glAttachShader(prog, fs);
            GLES20.glLinkProgram(prog);
            int[] linked = new int[1];
            GLES20.glGetProgramiv(prog, GLES20.GL_LINK_STATUS, linked, 0);
            if (linked[0] == 0) {
                throw new RuntimeException("program link failed: "
                        + GLES20.glGetProgramInfoLog(prog));
            }
            return prog;
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
