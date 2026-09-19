package enhance;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL14;
import org.lwjgl.opengl.GL15;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL21;
import org.lwjgl.opengl.GL30;
import org.lwjgl.opengl.GL33;

/**
 * In-world geometry renderer.
 *
 * This class never touches a disk: the native side compiles it ahead of time,
 * embeds the resulting bytecode in the DLL and hands it to
 * {@code JNIEnv::DefineClass} at runtime, into Fabric's class loader so
 * {@code org.lwjgl.*} resolves.
 *
 * It deliberately depends on nothing from Minecraft. Every Minecraft type is
 * obfuscated and renamed between builds, whereas the LWJGL names below are
 * stable, so this file keeps compiling and keeps linking across game updates.
 *
 * {@link #render} is called from inside Minecraft's world render pass, so the
 * game's framebuffer is bound and its depth attachment is still live — that is
 * what makes {@code depthTest} produce real occlusion against terrain, and what
 * keeps the HUD drawing on top afterwards.
 *
 * <p>It also doubles as the {@link InvocationHandler} behind the proxy that
 * subscribes to Fabric's {@code WorldRenderEvents}. That is how the renderer
 * gets its once-per-frame call now: hooking Minecraft's own render methods was
 * abandoned because every candidate is rewritten by Mixin on a modded install
 * — Sodium and Iris own {@code WorldRenderer.render}, Fabric API wraps
 * operations inside {@code InGameHud.render} — and JNIHook replaces the body it
 * hooks, which broke whichever mod owned that code. Subscribing to the event
 * redefines nothing.
 *
 * <p>Implementing the event interface directly would drag fabric-api into the
 * compile, and it is not in this repo. A {@code Proxy} needs only the interface
 * that already exists in the running game, so this class stays compilable
 * against nothing but the JDK and LWJGL.
 */
public final class EnhanceRenderer implements InvocationHandler {

    /**
     * Which job this handler instance has. One class serves three proxies: the
     * Fabric world render event, and one each for the triangle and line halves
     * of the submit-node path. The two submit proxies are separate instances
     * because a submitted node is drawn later -- a flag set at submit time
     * would already be stale by the time the callback runs.
     */
    public static final int KIND_WORLD_EVENT = 0;
    public static final int KIND_SUBMIT_TRIS = 1;
    public static final int KIND_SUBMIT_LINES = 2;

    private final int kind;

    // --- the game's vertex API, resolved by the native side ------------------
    //
    // This class compiles against the JDK and LWJGL only, so it cannot name
    // VertexConsumer or PoseStack.Pose. Nor can it look the methods up by name
    // itself: on a vanilla jar they are obfuscated, and only the native side
    // holds the mappings. So it is handed both the classes and the names.
    private static Method mAddVertex;   // (Pose, float, float, float) -> VertexConsumer
    private static Method mSetColor;    // (int, int, int, int)        -> VertexConsumer
    private static Method mSetNormal;   // (Pose, float, float, float) -> VertexConsumer
    private static Method mSetLineWidth; // (float)                    -> VertexConsumer
    private static Method mSetUv;       // (float, float)              -> VertexConsumer
    private static Method mSetUv1;      // (int, int)                  -> VertexConsumer
    private static Method mSetUv2;      // (int, int)                  -> VertexConsumer

    /**
     * Which elements a render type declares. The native side reads them off the
     * format itself and passes them here as a mask; this class fills exactly
     * those and nothing else.
     *
     * Guessing is not survivable: a vertex short of an element makes
     * BufferBuilder.build() throw from inside the game's own drain, long after
     * this callback has returned, where no catch here can reach it.
     */
    public static final int ELEM_COLOR = 1;
    public static final int ELEM_NORMAL = 2;
    public static final int ELEM_UV0 = 4;
    public static final int ELEM_UV1 = 8;
    public static final int ELEM_UV2 = 16;
    public static final int ELEM_LINE_WIDTH = 32;

    private static int trisElements;
    private static int linesElements;

    /** Full-bright, so the boxes do not take the world's lighting. */
    private static final int FULL_BRIGHT = 15;

    /** Tuned by eye; only ever sent when the format actually asks for it. */
    private static final float LINE_WIDTH = 2.0f;

    // The staged geometry the submit callbacks read. Published once per frame,
    // before submitting, and read back during the drain.
    private static ByteBuffer submitBuffer;
    private static int submitTriVertices;
    private static int submitLineVertices;

    /** x, y, z, r, g, b, a */
    private static final int FLOATS_PER_VERTEX = 7;
    private static final int STRIDE = FLOATS_PER_VERTEX * 4;

    private static final String VERTEX_SHADER =
        "#version 330 core\n" +
        "layout(location = 0) in vec3 in_pos;\n" +
        "layout(location = 1) in vec4 in_col;\n" +
        "uniform mat4 u_mvp;\n" +
        "out vec4 v_col;\n" +
        "void main() {\n" +
        "    v_col = in_col;\n" +
        "    gl_Position = u_mvp * vec4(in_pos, 1.0);\n" +
        "}\n";

    private static final String FRAGMENT_SHADER =
        "#version 330 core\n" +
        "in vec4 v_col;\n" +
        "out vec4 frag;\n" +
        "void main() {\n" +
        "    frag = v_col;\n" +
        "}\n";

    /** x, y, z, u, v, r, g, b, a */
    private static final int TEXT_FLOATS_PER_VERTEX = 9;
    private static final int TEXT_STRIDE = TEXT_FLOATS_PER_VERTEX * 4;

    private static final String TEXT_VERTEX_SHADER =
        "#version 330 core\n" +
        "layout(location = 0) in vec3 in_pos;\n" +
        "layout(location = 1) in vec2 in_uv;\n" +
        "layout(location = 2) in vec4 in_col;\n" +
        "uniform mat4 u_mvp;\n" +
        "out vec2 v_uv;\n" +
        "out vec4 v_col;\n" +
        "void main() {\n" +
        "    v_uv = in_uv;\n" +
        "    v_col = in_col;\n" +
        "    gl_Position = u_mvp * vec4(in_pos, 1.0);\n" +
        "}\n";

    // One shader covers both the glyphs and the panel behind them: the panel
    // samples the atlas's white pixel, so text and backdrop are the same draw
    // call and cannot get out of order with each other.
    private static final String TEXT_FRAGMENT_SHADER =
        "#version 330 core\n" +
        "in vec2 v_uv;\n" +
        "in vec4 v_col;\n" +
        "uniform sampler2D u_tex;\n" +
        "out vec4 frag;\n" +
        "void main() {\n" +
        "    frag = v_col * texture(u_tex, v_uv);\n" +
        "}\n";

    private static int program = 0;
    private static int vao = 0;
    private static int vbo = 0;
    private static int uMvp = -1;

    private static int textProgram = 0;
    private static int textVao = 0;
    private static int textVbo = 0;
    private static int textUMvp = -1;
    private static int fontTexture = 0;
    private static boolean textBroken = false;

    /** Set once the GL objects could not be built; stops retrying every frame. */
    private static boolean broken = false;

    /**
     * The handler the live proxy delegates to.
     *
     * Fabric's {@code Event} has no unregister, so a proxy handed to it stays
     * subscribed for the life of the JVM — including across an unload and a
     * fresh injection, which would otherwise leave the previous session's proxy
     * still firing and draw everything twice. Listeners compare themselves
     * against this field and stale ones return immediately.
     */
    private static volatile Object current = null;

    /**
     * Cleared by the native side before it unloads. The native method vanishes
     * with the DLL, so calling it afterwards has to stop happening; the
     * try/catch in {@link #invoke} is the second line of defence for the frame
     * that is already in flight.
     */
    private static volatile boolean active = false;

    /** The Fabric world render listener. */
    public EnhanceRenderer() {
        this(KIND_WORLD_EVENT);
    }

    private EnhanceRenderer(int kind) {
        this.kind = kind;
    }

    /** Native entry point: collects the frame's boxes and draws them. */
    /**
     * Binds the game's vertex API. Called once from the native side, with the
     * classes and the method names it resolved through the mappings.
     *
     * @return true when all three resolved; the submit path stays off otherwise
     *         rather than failing later, inside a frame.
     */
    public static boolean bindVertexApi(Class<?> consumerClass, Class<?> poseClass,
                                        String addVertex, String setColor, String setNormal,
                                        String setLineWidth, String setUv,
                                        String setUv1, String setUv2) {
        try {
            mAddVertex = consumerClass.getMethod(addVertex, poseClass,
                                                 float.class, float.class, float.class);
            mSetColor = consumerClass.getMethod(setColor,
                                                int.class, int.class, int.class, int.class);
            mSetNormal = consumerClass.getMethod(setNormal, poseClass,
                                                 float.class, float.class, float.class);
            mSetLineWidth = optional(consumerClass, setLineWidth, float.class);
            mSetUv = optional(consumerClass, setUv, float.class, float.class);
            mSetUv1 = optional(consumerClass, setUv1, int.class, int.class);
            mSetUv2 = optional(consumerClass, setUv2, int.class, int.class);
            return true;
        } catch (Throwable t) {
            mAddVertex = null;
            mSetColor = null;
            mSetNormal = null;
            return false;
        }
    }

    /**
     * Not every version has every setter -- setLineWidth arrives in 1.21.11.
     * A missing one is only fatal if a format actually asks for that element,
     * which the native side checks before enabling the render type.
     */
    private static Method optional(Class<?> owner, String name, Class<?>... args) {
        if (name == null || name.isEmpty()) {
            return null;
        }
        try {
            return owner.getMethod(name, args);
        } catch (Throwable t) {
            return null;
        }
    }

    /** Which elements each half's render type declares. */
    public static void setElementMasks(int tris, int lines) {
        trisElements = tris;
        linesElements = lines;
    }

    /** Whether every element in the mask has a setter bound. */
    public static boolean canFill(int mask) {
        if (mAddVertex == null || mSetColor == null) {
            return false;
        }
        return ((mask & ELEM_NORMAL) == 0 || mSetNormal != null)
            && ((mask & ELEM_UV0) == 0 || mSetUv != null)
            && ((mask & ELEM_UV1) == 0 || mSetUv1 != null)
            && ((mask & ELEM_UV2) == 0 || mSetUv2 != null)
            && ((mask & ELEM_LINE_WIDTH) == 0 || mSetLineWidth != null);
    }

    /** Publishes this frame's geometry for the submit callbacks to read. */
    public static void stageGeometry(ByteBuffer buffer, int triVertices, int lineVertices) {
        submitBuffer = buffer;
        submitTriVertices = triVertices;
        submitLineVertices = lineVertices;
    }

    /** A proxy handler bound to one of the two submit ranges. */
    public static Object submitHandler(int kind) {
        return new EnhanceRenderer(kind);
    }

    private static int channel(float f) {
        int v = (int) (f * 255.0f + 0.5f);
        if (v < 0) {
            v = 0;
        }
        return v > 255 ? 255 : v;
    }

    /**
     * Feeds one range of the staged geometry to the game's vertex consumer.
     *
     * Two reflective calls per vertex, three for lines. At a few thousand
     * vertices a frame that is well under a tenth of a millisecond, and it buys
     * a renderer that never names OpenGL or Vulkan: whichever backend the game
     * is running draws this, because the consumer is the game's own.
     */
    private static void emit(Object pose, Object consumer, int first, int count,
                             boolean lines, int elements) {
        final ByteBuffer buf = submitBuffer;
        if (buf == null || mAddVertex == null || count <= 0) {
            return;
        }

        try {
            for (int v = 0; v < count; v++) {
                final int base = (first + v) * STRIDE;

                final float x = buf.getFloat(base);
                final float y = buf.getFloat(base + 4);
                final float z = buf.getFloat(base + 8);

                mAddVertex.invoke(consumer, pose, Float.valueOf(x), Float.valueOf(y),
                                  Float.valueOf(z));

                if ((elements & ELEM_COLOR) != 0) {
                    mSetColor.invoke(consumer,
                                     Integer.valueOf(channel(buf.getFloat(base + 12))),
                                     Integer.valueOf(channel(buf.getFloat(base + 16))),
                                     Integer.valueOf(channel(buf.getFloat(base + 20))),
                                     Integer.valueOf(channel(buf.getFloat(base + 24))));
                }
                if ((elements & ELEM_UV0) != 0) {
                    mSetUv.invoke(consumer, Float.valueOf(0.0f), Float.valueOf(0.0f));
                }
                if ((elements & ELEM_UV1) != 0) {
                    mSetUv1.invoke(consumer, Integer.valueOf(0), Integer.valueOf(10));
                }
                if ((elements & ELEM_UV2) != 0) {
                    mSetUv2.invoke(consumer, Integer.valueOf(FULL_BRIGHT),
                                   Integer.valueOf(FULL_BRIGHT));
                }
                if ((elements & ELEM_LINE_WIDTH) != 0) {
                    mSetLineWidth.invoke(consumer, Float.valueOf(LINE_WIDTH));
                }

                if ((elements & ELEM_NORMAL) != 0) {
                    // The line render type carries a normal and the game reads it
                    // as the segment's direction, which is what gives the line its
                    // width. Vertices arrive in pairs, so both ends of a segment
                    // take the direction of the pair they belong to.
                    // For lines the normal is the segment direction. For a filled
                    // shape the format rarely asks for one, and straight up is a
                    // harmless answer if it does.
                    final int mate = lines
                        ? ((v & 1) == 0 ? (first + v + 1) : (first + v - 1)) * STRIDE
                        : base;
                    float nx = buf.getFloat(mate) - x;
                    float ny = buf.getFloat(mate + 4) - y;
                    float nz = buf.getFloat(mate + 8) - z;
                    if ((v & 1) == 1) {
                        nx = -nx;
                        ny = -ny;
                        nz = -nz;
                    }
                    final float len = (float) Math.sqrt(nx * nx + ny * ny + nz * nz);
                    if (len > 1.0e-5f) {
                        nx /= len;
                        ny /= len;
                        nz /= len;
                    } else {
                        nx = 0.0f;
                        ny = 1.0f;
                        nz = 0.0f;
                    }
                    mSetNormal.invoke(consumer, pose, Float.valueOf(nx), Float.valueOf(ny),
                                      Float.valueOf(nz));
                }
            }
        } catch (Throwable t) {
            // Nothing may escape into the game's render pass: it would take the
            // frame with it. Dropping the binding turns the path off instead.
            mAddVertex = null;
        }
    }

    private static native void frame();

    /** Called from native code — see the fields above. */
    public static void setCurrent(Object handler) {
        current = handler;
    }

    public static void setActive(boolean value) {
        active = value;
    }

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) {
        final String name = method.getName();

        // Object's own methods are routed to the handler too, and the proxy
        // unboxes what comes back — returning null for hashCode or equals
        // throws inside the proxy rather than here.
        if ("hashCode".equals(name)) {
            return Integer.valueOf(System.identityHashCode(proxy));
        }
        if ("equals".equals(name)) {
            return Boolean.valueOf(proxy == (args == null || args.length == 0 ? null : args[0]));
        }
        if ("toString".equals(name)) {
            return kind == KIND_WORLD_EVENT
                ? "EnhanceRenderer$WorldRenderListener"
                : "EnhanceRenderer$SubmitGeometry";
        }

        // The submit proxies are stateless and have nothing to do with the
        // Fabric listener's lifecycle, so they are dispatched ahead of its guards.
        if (kind != KIND_WORLD_EVENT) {
            if (args != null && args.length >= 2) {
                final boolean lines = kind == KIND_SUBMIT_LINES;
                emit(args[0], args[1],
                     lines ? submitTriVertices : 0,
                     lines ? submitLineVertices : submitTriVertices,
                     lines,
                     lines ? linesElements : trisElements);
            }
            return null;
        }

        if (!active || this != current) {
            return null;
        }

        // Nothing may escape into the render event: an exception thrown here
        // propagates into Minecraft's world render pass and takes the frame
        // with it. UnsatisfiedLinkError is the expected one, from a native
        // method that unregistered between the check above and this call.
        try {
            frame();
        } catch (Throwable t) {
            active = false;
        }

        return null;
    }

    /**
     * @param verts           direct buffer owned by native code: triangle vertices
     *                        first, then line vertices, each x,y,z,r,g,b,a floats,
     *                        positions relative to the camera
     * @param triVertexCount  number of triangle vertices at the start of the buffer
     * @param lineVertexCount number of line vertices following them
     * @param mvp             column-major view-projection matrix, 16 floats
     * @param depthTest       true clips against the world's depth buffer, false
     *                        draws through terrain
     */
    public static void render(ByteBuffer verts,
                              int triVertexCount,
                              int lineVertexCount,
                              float[] mvp,
                              boolean depthTest) {
        if (broken || verts == null || mvp == null || mvp.length < 16) {
            return;
        }
        if (triVertexCount <= 0 && lineVertexCount <= 0) {
            return;
        }
        if (!ensureResources()) {
            return;
        }

        final int totalVertices = triVertexCount + lineVertexCount;

        // The bytes were written natively, so the upload is order-agnostic, but
        // LWJGL validates the order of buffers handed to it.
        verts.order(ByteOrder.nativeOrder());
        verts.position(0);
        verts.limit(totalVertices * STRIDE);

        final int prevProgram = GL11.glGetInteger(GL20.GL_CURRENT_PROGRAM);
        final int prevVao = GL11.glGetInteger(GL30.GL_VERTEX_ARRAY_BINDING);
        final int prevVbo = GL11.glGetInteger(GL15.GL_ARRAY_BUFFER_BINDING);
        final boolean prevDepth = GL11.glIsEnabled(GL11.GL_DEPTH_TEST);
        final boolean prevBlend = GL11.glIsEnabled(GL11.GL_BLEND);
        final boolean prevCull = GL11.glIsEnabled(GL11.GL_CULL_FACE);
        final boolean prevScissor = GL11.glIsEnabled(GL11.GL_SCISSOR_TEST);

        if (depthTest) {
            GL11.glEnable(GL11.GL_DEPTH_TEST);
        } else {
            GL11.glDisable(GL11.GL_DEPTH_TEST);
        }

        // Read the world's depth, never write to it: a translucent fill that
        // wrote depth would occlude whatever the game draws later in the pass.
        // Restored to enabled below rather than read back first — the world pass
        // runs with depth writes on, and reading the mask would pull in a
        // wider slice of the LWJGL surface than is worth the risk.
        GL11.glDepthMask(false);
        GL11.glDisable(GL11.GL_CULL_FACE);
        GL11.glDisable(GL11.GL_SCISSOR_TEST);
        GL11.glEnable(GL11.GL_BLEND);
        GL11.glBlendFunc(GL11.GL_SRC_ALPHA, GL11.GL_ONE_MINUS_SRC_ALPHA);

        GL20.glUseProgram(program);
        if (uMvp >= 0) {
            GL20.glUniformMatrix4fv(uMvp, false, mvp);
        }

        GL30.glBindVertexArray(vao);
        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, vbo);
        GL15.glBufferData(GL15.GL_ARRAY_BUFFER, verts, GL15.GL_STREAM_DRAW);

        if (triVertexCount > 0) {
            GL11.glDrawArrays(GL11.GL_TRIANGLES, 0, triVertexCount);
        }
        if (lineVertexCount > 0) {
            GL11.glDrawArrays(GL11.GL_LINES, triVertexCount, lineVertexCount);
        }

        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, prevVbo);
        GL30.glBindVertexArray(prevVao);
        GL20.glUseProgram(prevProgram);

        GL11.glDepthMask(true);
        setEnabled(GL11.GL_DEPTH_TEST, prevDepth);
        setEnabled(GL11.GL_BLEND, prevBlend);
        setEnabled(GL11.GL_CULL_FACE, prevCull);
        setEnabled(GL11.GL_SCISSOR_TEST, prevScissor);
    }

    /**
     * Uploads the glyph atlas. Called once, with ImGui's own font bitmap — the
     * client already has it rasterised, so the in-world tags use exactly the
     * font the 2D overlay does instead of shipping a second one.
     *
     * @param rgba direct buffer of w*h RGBA8 texels, owned by native code
     * @return false if the texture could not be created; text then stays off
     */
    public static int uploadFont(ByteBuffer rgba, int w, int h) {
        if (rgba == null || w <= 0 || h <= 0) {
            return -1;
        }

        if (fontTexture != 0) {
            GL11.glDeleteTextures(fontTexture);
            fontTexture = 0;
        }

        rgba.order(ByteOrder.nativeOrder());
        rgba.position(0);
        rgba.limit(w * h * 4);

        // Unit 0 explicitly, and restored afterwards. Minecraft leaves whatever
        // unit its last draw used bound — lightmap on 1, overlay on 2 — and
        // uploading into that one puts the atlas somewhere the sampler never
        // looks.
        final int prevUnit = GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);
        GL13.glActiveTexture(GL13.GL_TEXTURE0);

        final int prevTexture = GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);

        // A bound pixel-unpack buffer silently changes what the pointer means:
        // glTexImage2D would read the client pointer as an offset INTO that
        // buffer and fill the texture with whatever lives there, reporting no
        // error at all. Minecraft and Sodium both keep buffers bound, so this
        // has to be cleared rather than assumed empty. Same for the unpack
        // row/skip state, which is per-context and not ours.
        final int prevUnpack = GL11.glGetInteger(GL21.GL_PIXEL_UNPACK_BUFFER_BINDING);
        if (prevUnpack != 0) {
            GL15.glBindBuffer(GL21.GL_PIXEL_UNPACK_BUFFER, 0);
        }

        final int prevAlign = GL11.glGetInteger(GL11.GL_UNPACK_ALIGNMENT);
        final int prevRowLength = GL11.glGetInteger(GL11.GL_UNPACK_ROW_LENGTH);
        final int prevSkipPixels = GL11.glGetInteger(GL11.GL_UNPACK_SKIP_PIXELS);
        final int prevSkipRows = GL11.glGetInteger(GL11.GL_UNPACK_SKIP_ROWS);
        GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, 4);
        GL11.glPixelStorei(GL11.GL_UNPACK_ROW_LENGTH, 0);
        GL11.glPixelStorei(GL11.GL_UNPACK_SKIP_PIXELS, 0);
        GL11.glPixelStorei(GL11.GL_UNPACK_SKIP_ROWS, 0);

        // Drain any error the game left pending, so what comes back below is
        // attributable to this upload.
        while (GL11.glGetError() != GL11.GL_NO_ERROR) {
            // discard
        }

        fontTexture = GL11.glGenTextures();
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, fontTexture);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, GL11.GL_LINEAR);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_LINEAR);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_S, GL12.GL_CLAMP_TO_EDGE);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_T, GL12.GL_CLAMP_TO_EDGE);
        GL11.glTexImage2D(GL11.GL_TEXTURE_2D, 0, GL11.GL_RGBA, w, h, 0,
                          GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, rgba);

        // Belt and braces against the same incompleteness the sampler unbind
        // in renderText addresses: with a full mip chain present, a stray
        // mipmap filter from anywhere still resolves instead of sampling black.
        GL30.glGenerateMipmap(GL11.GL_TEXTURE_2D);

        int status = GL11.glGetError();

        // Read the texture back and compare it to what was handed in.
        //
        // "Contains a non-zero byte" is not good enough: the atlas is white
        // everywhere with coverage in the alpha channel, so an upload that
        // dropped alpha entirely still passes that test while rendering as
        // invisible glyphs on a solid panel — which is exactly the symptom
        // this went looking for.
        if (status == GL11.GL_NO_ERROR) {
            final int bytes = w * h * 4;
            final ByteBuffer check = ByteBuffer.allocateDirect(bytes).order(ByteOrder.nativeOrder());
            GL11.glGetTexImage(GL11.GL_TEXTURE_2D, 0, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, check);

            if (GL11.glGetError() == GL11.GL_NO_ERROR) {
                int mismatches = 0;
                for (int i = 0; i < bytes; ++i) {
                    if (check.get(i) != rgba.get(i)) {
                        if (++mismatches > 16) {
                            break;
                        }
                    }
                }
                if (mismatches > 16) {
                    status = -2;
                }
            }
        }

        GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, prevAlign);
        GL11.glPixelStorei(GL11.GL_UNPACK_ROW_LENGTH, prevRowLength);
        GL11.glPixelStorei(GL11.GL_UNPACK_SKIP_PIXELS, prevSkipPixels);
        GL11.glPixelStorei(GL11.GL_UNPACK_SKIP_ROWS, prevSkipRows);

        if (prevUnpack != 0) {
            GL15.glBindBuffer(GL21.GL_PIXEL_UNPACK_BUFFER, prevUnpack);
        }

        GL11.glBindTexture(GL11.GL_TEXTURE_2D, prevTexture);
        GL13.glActiveTexture(prevUnit);

        if (fontTexture == 0) {
            return -1;
        }

        return status;
    }

    /**
     * Draws billboarded text quads: panel first, glyphs after, both sampling
     * the atlas.
     *
     * @param verts       direct buffer, x,y,z,u,v,r,g,b,a per vertex, camera-relative
     * @param vertexCount total vertices, drawn as triangles
     * @param mvp         column-major view-projection, 16 floats
     * @param depthTest   true clips the tags against terrain
     */
    public static void renderText(ByteBuffer verts,
                                  int vertexCount,
                                  float[] mvp,
                                  boolean depthTest) {
        if (textBroken || fontTexture == 0 || verts == null || mvp == null || mvp.length < 16) {
            return;
        }
        if (vertexCount <= 0) {
            return;
        }
        if (!ensureTextResources()) {
            return;
        }

        verts.order(ByteOrder.nativeOrder());
        verts.position(0);
        verts.limit(vertexCount * TEXT_STRIDE);

        final int prevProgram = GL11.glGetInteger(GL20.GL_CURRENT_PROGRAM);
        final int prevVao = GL11.glGetInteger(GL30.GL_VERTEX_ARRAY_BINDING);
        final int prevVbo = GL11.glGetInteger(GL15.GL_ARRAY_BUFFER_BINDING);

        // The sampler is bound to unit 0, so the atlas has to go there — see
        // uploadFont. Binding to whichever unit Minecraft left active makes
        // every glyph sample an unbound texture, which a core profile reports
        // as opaque black: the tags come out as solid rectangles.
        final int prevUnit = GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);
        GL13.glActiveTexture(GL13.GL_TEXTURE0);
        final int prevTexture = GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);

        // A sampler object bound to this unit OVERRIDES the texture's own
        // parameters. Iris keeps one on unit 0, and its mipmap filter makes an
        // atlas with no mipmaps an incomplete texture — which samples as opaque
        // black across the whole quad, not as nothing. That is the difference
        // between "glyph-shaped and invisible" and the solid rectangles this
        // was producing, and it is why setting glTexParameteri on our own
        // texture changed nothing.
        final int prevSampler = GL11.glGetInteger(GL33.GL_SAMPLER_BINDING);
        if (prevSampler != 0) {
            GL33.glBindSampler(0, 0);
        }

        final boolean prevDepth = GL11.glIsEnabled(GL11.GL_DEPTH_TEST);
        final boolean prevBlend = GL11.glIsEnabled(GL11.GL_BLEND);
        final boolean prevCull = GL11.glIsEnabled(GL11.GL_CULL_FACE);
        final boolean prevScissor = GL11.glIsEnabled(GL11.GL_SCISSOR_TEST);

        if (depthTest) {
            GL11.glEnable(GL11.GL_DEPTH_TEST);
        } else {
            GL11.glDisable(GL11.GL_DEPTH_TEST);
        }

        GL11.glDepthMask(false);
        GL11.glDisable(GL11.GL_CULL_FACE);
        GL11.glDisable(GL11.GL_SCISSOR_TEST);
        GL11.glEnable(GL11.GL_BLEND);

        // Separate factors and an explicit equation, not just glBlendFunc.
        // The game leaves both set to whatever its last pass wanted, and
        // glBlendFunc alone does not reset the alpha channel or a non-additive
        // equation — a leftover of either turns white glyphs on a dark panel
        // into nothing at all.
        GL14.glBlendEquation(GL14.GL_FUNC_ADD);
        GL14.glBlendFuncSeparate(GL11.GL_SRC_ALPHA, GL11.GL_ONE_MINUS_SRC_ALPHA,
                                 GL11.GL_ONE, GL11.GL_ONE_MINUS_SRC_ALPHA);

        // Nothing here writes to a depth-tested-away pixel, but the game can
        // leave a colour mask on; text is the only thing that would show it.
        GL11.glColorMask(true, true, true, true);

        GL20.glUseProgram(textProgram);
        if (textUMvp >= 0) {
            GL20.glUniformMatrix4fv(textUMvp, false, mvp);
        }

        GL11.glBindTexture(GL11.GL_TEXTURE_2D, fontTexture);

        GL30.glBindVertexArray(textVao);
        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, textVbo);
        GL15.glBufferData(GL15.GL_ARRAY_BUFFER, verts, GL15.GL_STREAM_DRAW);
        GL11.glDrawArrays(GL11.GL_TRIANGLES, 0, vertexCount);

        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, prevVbo);
        GL30.glBindVertexArray(prevVao);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, prevTexture);
        if (prevSampler != 0) {
            GL33.glBindSampler(0, prevSampler);
        }
        GL13.glActiveTexture(prevUnit);
        GL20.glUseProgram(prevProgram);

        GL11.glDepthMask(true);
        setEnabled(GL11.GL_DEPTH_TEST, prevDepth);
        setEnabled(GL11.GL_BLEND, prevBlend);
        setEnabled(GL11.GL_CULL_FACE, prevCull);
        setEnabled(GL11.GL_SCISSOR_TEST, prevScissor);
    }

    private static boolean ensureTextResources() {
        if (textProgram != 0 && textVao != 0 && textVbo != 0) {
            return true;
        }

        final int vs = compile(GL20.GL_VERTEX_SHADER, TEXT_VERTEX_SHADER);
        if (vs == 0) {
            textBroken = true;
            return false;
        }

        final int fs = compile(GL20.GL_FRAGMENT_SHADER, TEXT_FRAGMENT_SHADER);
        if (fs == 0) {
            GL20.glDeleteShader(vs);
            textBroken = true;
            return false;
        }

        textProgram = GL20.glCreateProgram();
        GL20.glAttachShader(textProgram, vs);
        GL20.glAttachShader(textProgram, fs);
        GL20.glLinkProgram(textProgram);
        final int linked = GL20.glGetProgrami(textProgram, GL20.GL_LINK_STATUS);
        GL20.glDeleteShader(vs);
        GL20.glDeleteShader(fs);

        if (linked == GL11.GL_FALSE) {
            GL20.glDeleteProgram(textProgram);
            textProgram = 0;
            textBroken = true;
            return false;
        }

        textUMvp = GL20.glGetUniformLocation(textProgram, "u_mvp");

        // Sampler stays on unit 0, which is what the draw binds to.
        final int prevProgram = GL11.glGetInteger(GL20.GL_CURRENT_PROGRAM);
        GL20.glUseProgram(textProgram);
        final int uTex = GL20.glGetUniformLocation(textProgram, "u_tex");
        if (uTex >= 0) {
            GL20.glUniform1i(uTex, 0);
        }
        GL20.glUseProgram(prevProgram);

        final int prevVao = GL11.glGetInteger(GL30.GL_VERTEX_ARRAY_BINDING);
        final int prevVbo = GL11.glGetInteger(GL15.GL_ARRAY_BUFFER_BINDING);

        textVao = GL30.glGenVertexArrays();
        textVbo = GL15.glGenBuffers();

        GL30.glBindVertexArray(textVao);
        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, textVbo);
        GL20.glEnableVertexAttribArray(0);
        GL20.glVertexAttribPointer(0, 3, GL11.GL_FLOAT, false, TEXT_STRIDE, 0L);
        GL20.glEnableVertexAttribArray(1);
        GL20.glVertexAttribPointer(1, 2, GL11.GL_FLOAT, false, TEXT_STRIDE, 3L * 4L);
        GL20.glEnableVertexAttribArray(2);
        GL20.glVertexAttribPointer(2, 4, GL11.GL_FLOAT, false, TEXT_STRIDE, 5L * 4L);

        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, prevVbo);
        GL30.glBindVertexArray(prevVao);

        return textVao != 0 && textVbo != 0;
    }

    /** Frees the GL objects. Must be called with the context current. */
    public static void dispose() {
        if (fontTexture != 0) {
            GL11.glDeleteTextures(fontTexture);
            fontTexture = 0;
        }
        if (textVbo != 0) {
            GL15.glDeleteBuffers(textVbo);
            textVbo = 0;
        }
        if (textVao != 0) {
            GL30.glDeleteVertexArrays(textVao);
            textVao = 0;
        }
        if (textProgram != 0) {
            GL20.glDeleteProgram(textProgram);
            textProgram = 0;
        }
        textUMvp = -1;
        textBroken = false;

        disposeGeometry();
    }

    private static void disposeGeometry() {
        if (vbo != 0) {
            GL15.glDeleteBuffers(vbo);
            vbo = 0;
        }
        if (vao != 0) {
            GL30.glDeleteVertexArrays(vao);
            vao = 0;
        }
        if (program != 0) {
            GL20.glDeleteProgram(program);
            program = 0;
        }
        uMvp = -1;
        broken = false;
    }

    private static void setEnabled(int cap, boolean enabled) {
        if (enabled) {
            GL11.glEnable(cap);
        } else {
            GL11.glDisable(cap);
        }
    }

    private static boolean ensureResources() {
        if (program != 0 && vao != 0 && vbo != 0) {
            return true;
        }

        final int vs = compile(GL20.GL_VERTEX_SHADER, VERTEX_SHADER);
        if (vs == 0) {
            broken = true;
            return false;
        }

        final int fs = compile(GL20.GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
        if (fs == 0) {
            GL20.glDeleteShader(vs);
            broken = true;
            return false;
        }

        program = GL20.glCreateProgram();
        GL20.glAttachShader(program, vs);
        GL20.glAttachShader(program, fs);
        GL20.glLinkProgram(program);
        final int linked = GL20.glGetProgrami(program, GL20.GL_LINK_STATUS);
        GL20.glDeleteShader(vs);
        GL20.glDeleteShader(fs);

        if (linked == GL11.GL_FALSE) {
            GL20.glDeleteProgram(program);
            program = 0;
            broken = true;
            return false;
        }

        uMvp = GL20.glGetUniformLocation(program, "u_mvp");

        final int prevVao = GL11.glGetInteger(GL30.GL_VERTEX_ARRAY_BINDING);
        final int prevVbo = GL11.glGetInteger(GL15.GL_ARRAY_BUFFER_BINDING);

        vao = GL30.glGenVertexArrays();
        vbo = GL15.glGenBuffers();

        GL30.glBindVertexArray(vao);
        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, vbo);
        GL20.glEnableVertexAttribArray(0);
        GL20.glVertexAttribPointer(0, 3, GL11.GL_FLOAT, false, STRIDE, 0L);
        GL20.glEnableVertexAttribArray(1);
        GL20.glVertexAttribPointer(1, 4, GL11.GL_FLOAT, false, STRIDE, 3L * 4L);

        GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, prevVbo);
        GL30.glBindVertexArray(prevVao);

        return vao != 0 && vbo != 0;
    }

    private static int compile(int type, String source) {
        final int shader = GL20.glCreateShader(type);
        if (shader == 0) {
            return 0;
        }
        GL20.glShaderSource(shader, source);
        GL20.glCompileShader(shader);
        if (GL20.glGetShaderi(shader, GL20.GL_COMPILE_STATUS) == GL11.GL_FALSE) {
            GL20.glDeleteShader(shader);
            return 0;
        }
        return shader;
    }
}
