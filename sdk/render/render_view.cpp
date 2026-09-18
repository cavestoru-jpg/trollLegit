#include "render_view.h"

#include <enhance/enhance.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <sdk/compat/compat.h>
#include <sdk/java/interp.h>

#define _USE_MATH_DEFINES
#include <cmath>
#include <cfloat>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
	constexpr float k_near = 0.01f;

	sdk::render::view_t g_view{};

	void clear_exception(JNIEnv* env)
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
	}
}

JNIEnv* sdk::render::current_thread_env()
{
	if (!enhance::instance)
		return nullptr;

	JavaVM* vm = enhance::instance->get_java_vm();
	if (!vm)
		return nullptr;

	JNIEnv* env = nullptr;
	const jint rc = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);
	if (rc == JNI_OK && env)
		return env;

	// The render thread is one of the JVM's own threads, so GetEnv normally
	// succeeds. Attaching is the fallback for anything else that ends up here.
	if (rc == JNI_EDETACHED && vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) == JNI_OK)
		return env;

	return nullptr;
}

bool sdk::render::sample_camera(double& out_x, double& out_y, double& out_z,
                                 float& out_yaw, float& out_pitch, float& out_fov)
{
	JNIEnv* env = current_thread_env();
	if (!env)
		return false;

	bool ok = false;

	jclass minecraft_class = sdk::classloader::find_class(env, sdk::mappings::minecraftclass_sig);
	if (!minecraft_class)
		return false;

	jobject minecraft = nullptr;
	jobject game_renderer = nullptr;
	jclass gamerenderer_class = nullptr;
	jobject camera = nullptr;
	jclass camera_class = nullptr;
	jobject pos_vec3d = nullptr;
	jclass vec3d_class = nullptr;

	do
	{
		jfieldID instance_fid = env->GetStaticFieldID(minecraft_class, sdk::mappings::minecraftclient_name, sdk::mappings::minecraftclient_sig);
		clear_exception(env);
		if (!instance_fid) break;

		minecraft = env->GetStaticObjectField(minecraft_class, instance_fid);
		clear_exception(env);
		if (!minecraft) break;

		jfieldID gamerenderer_fid = env->GetFieldID(minecraft_class, sdk::mappings::gamerenderer_name, sdk::mappings::gamerenderer_sig);
		clear_exception(env);
		if (!gamerenderer_fid) break;

		game_renderer = env->GetObjectField(minecraft, gamerenderer_fid);
		clear_exception(env);
		if (!game_renderer) break;

		gamerenderer_class = sdk::classloader::find_class(env, sdk::mappings::gamerenderer_class_sig);
		if (!gamerenderer_class) break;

		jmethodID get_camera_mid = env->GetMethodID(gamerenderer_class, sdk::mappings::get_camera_name, sdk::mappings::get_camera_sig);
		clear_exception(env);
		if (!get_camera_mid) break;

		camera = env->CallObjectMethod(game_renderer, get_camera_mid);
		clear_exception(env);
		if (!camera) break;

		camera_class = sdk::classloader::find_class(env, sdk::mappings::camera_class_sig);
		if (!camera_class) break;

		jfieldID camera_pos_fid = env->GetFieldID(camera_class, sdk::mappings::camera_pos_field_name, sdk::mappings::camera_pos_field_sig);
		clear_exception(env);
		jmethodID camera_yaw_mid = env->GetMethodID(camera_class, sdk::mappings::camera_get_yaw_name, sdk::mappings::camera_get_yaw_sig);
		clear_exception(env);
		jmethodID camera_pitch_mid = env->GetMethodID(camera_class, sdk::mappings::camera_get_pitch_name, sdk::mappings::camera_get_pitch_sig);
		clear_exception(env);
		if (!camera_pos_fid || !camera_yaw_mid || !camera_pitch_mid) break;

		pos_vec3d = env->GetObjectField(camera, camera_pos_fid);
		clear_exception(env);
		if (!pos_vec3d) break;

		vec3d_class = sdk::classloader::find_class(env, sdk::mappings::vec3d_class_sig);
		if (!vec3d_class) break;

		jfieldID vx = env->GetFieldID(vec3d_class, sdk::mappings::vec3d_x_name, sdk::mappings::vec3d_x_sig);
		clear_exception(env);
		jfieldID vy = env->GetFieldID(vec3d_class, sdk::mappings::vec3d_y_name, sdk::mappings::vec3d_y_sig);
		clear_exception(env);
		jfieldID vz = env->GetFieldID(vec3d_class, sdk::mappings::vec3d_z_name, sdk::mappings::vec3d_z_sig);
		clear_exception(env);
		if (!vx || !vy || !vz) break;

		out_x = env->GetDoubleField(pos_vec3d, vx);
		out_y = env->GetDoubleField(pos_vec3d, vy);
		out_z = env->GetDoubleField(pos_vec3d, vz);
		out_yaw = env->CallFloatMethod(camera, camera_yaw_mid);
		clear_exception(env);
		out_pitch = env->CallFloatMethod(camera, camera_pitch_mid);
		clear_exception(env);

		// The game builds its own projection with the live tick fraction, so
		// passing a constant here asks for a different FOV than the one the frame
		// was rendered with — which radially mis-scales every box for the ~0.25 s
		// the FOV takes to settle after a sprint, bow pull or spyglass.
		//
		// Through the compat helper because the call shape is version-specific:
		// a three-argument method on GameRenderer returning double, then float,
		// and from 26.1 a no-argument getter on Camera.
		const float fov = sdk::compat::field_of_view(env, game_renderer, camera,
		                                             sdk::java::tick_progress());
		if (fov > 0.0f)
			out_fov = fov;

		ok = true;
	} while (false);

	if (vec3d_class) env->DeleteLocalRef(vec3d_class);
	if (pos_vec3d) env->DeleteLocalRef(pos_vec3d);
	if (camera_class) env->DeleteLocalRef(camera_class);
	if (camera) env->DeleteLocalRef(camera);
	if (gamerenderer_class) env->DeleteLocalRef(gamerenderer_class);
	if (game_renderer) env->DeleteLocalRef(game_renderer);
	if (minecraft) env->DeleteLocalRef(minecraft);
	env->DeleteLocalRef(minecraft_class);

	return ok;
}

void sdk::render::set_view(double cam_x, double cam_y, double cam_z,
                            float yaw, float pitch, float fov,
                            int screen_w, int screen_h)
{
	g_view.valid = false;

	if (screen_w <= 0 || screen_h <= 0 || fov <= 0.0f || fov >= 180.0f)
		return;

	const float yaw_rad = yaw * static_cast<float>(M_PI / 180.0);
	const float pitch_rad = pitch * static_cast<float>(M_PI / 180.0);

	const float cy = cosf(yaw_rad), sy = sinf(yaw_rad);
	const float cp = cosf(pitch_rad), sp = sinf(pitch_rad);

	// Minecraft's look vector: +X east, +Y up, +Z south, yaw 0 faces +Z and
	// positive pitch looks down.
	g_view.forward[0] = -sy * cp;
	g_view.forward[1] = -sp;
	g_view.forward[2] =  cy * cp;

	// right = normalize(forward x worldUp). Written out in closed form rather
	// than as a cross product + normalize: the cross collapses to zero length
	// when looking straight up or down, which used to leave the basis full of
	// garbage at pitch ±90.
	g_view.right[0] = -cy;
	g_view.right[1] =  0.0f;
	g_view.right[2] = -sy;

	// up = right x forward
	g_view.up[0] = g_view.right[1] * g_view.forward[2] - g_view.right[2] * g_view.forward[1];
	g_view.up[1] = g_view.right[2] * g_view.forward[0] - g_view.right[0] * g_view.forward[2];
	g_view.up[2] = g_view.right[0] * g_view.forward[1] - g_view.right[1] * g_view.forward[0];

	g_view.cam_x = cam_x;
	g_view.cam_y = cam_y;
	g_view.cam_z = cam_z;
	g_view.yaw = yaw;
	g_view.pitch = pitch;
	g_view.fov = fov;

	const float aspect = static_cast<float>(screen_w) / static_cast<float>(screen_h);
	const float tan_half = tanf(fov * static_cast<float>(M_PI / 180.0) * 0.5f);
	g_view.fov_x = 1.0f / (tan_half * aspect);
	g_view.fov_y = 1.0f / tan_half;

	g_view.half_w = screen_w * 0.5f;
	g_view.half_h = screen_h * 0.5f;

	g_view.valid = true;
}

const sdk::render::view_t& sdk::render::view()
{
	return g_view;
}

void sdk::render::invalidate_view()
{
	g_view.valid = false;
}

void sdk::render::to_view(float x, float y, float z, float& px, float& py, float& pz)
{
	const float lx = x - static_cast<float>(g_view.cam_x);
	const float ly = y - static_cast<float>(g_view.cam_y);
	const float lz = z - static_cast<float>(g_view.cam_z);

	px = lx * g_view.right[0] + ly * g_view.right[1] + lz * g_view.right[2];
	py = lx * g_view.up[0] + ly * g_view.up[1] + lz * g_view.up[2];
	pz = lx * g_view.forward[0] + ly * g_view.forward[1] + lz * g_view.forward[2];
}

namespace
{
	inline void view_to_screen(float px, float py, float pz, float& sx, float& sy)
	{
		const float inv_z = 1.0f / pz;
		sx = g_view.half_w + (px * inv_z * g_view.fov_x) * g_view.half_w;
		sy = g_view.half_h - (py * inv_z * g_view.fov_y) * g_view.half_h;
	}
}

bool sdk::render::world_to_screen(float x, float y, float z, float& sx, float& sy)
{
	if (!g_view.valid)
		return false;

	float px, py, pz;
	to_view(x, y, z, px, py, pz);
	if (pz < k_near)
		return false;

	view_to_screen(px, py, pz, sx, sy);
	return true;
}

bool sdk::render::project_segment(const float a[3], const float b[3],
                                   float out_a[2], float out_b[2])
{
	if (!g_view.valid)
		return false;

	float ax, ay, az, bx, by, bz;
	to_view(a[0], a[1], a[2], ax, ay, az);
	to_view(b[0], b[1], b[2], bx, by, bz);

	const bool a_behind = az < k_near;
	const bool b_behind = bz < k_near;

	if (a_behind && b_behind)
		return false;

	if (a_behind || b_behind)
	{
		// Slide the behind-camera endpoint up to the near plane.
		const float t = (k_near - az) / (bz - az);
		const float cx = ax + (bx - ax) * t;
		const float cy = ay + (by - ay) * t;

		if (a_behind) { ax = cx; ay = cy; az = k_near; }
		else          { bx = cx; by = cy; bz = k_near; }
	}

	view_to_screen(ax, ay, az, out_a[0], out_a[1]);
	view_to_screen(bx, by, bz, out_b[0], out_b[1]);
	return true;
}

const int sdk::render::box_edges[12][2] = {
	{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },   // bottom face
	{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },   // top face
	{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },   // verticals
};

void sdk::render::box_corners(const double mn[3], const double mx[3], float out[8][3])
{
	const float x0 = static_cast<float>(mn[0]), y0 = static_cast<float>(mn[1]), z0 = static_cast<float>(mn[2]);
	const float x1 = static_cast<float>(mx[0]), y1 = static_cast<float>(mx[1]), z1 = static_cast<float>(mx[2]);

	const float c[8][3] = {
		{ x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 },
		{ x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 },
	};

	for (int i = 0; i < 8; ++i)
		for (int j = 0; j < 3; ++j)
			out[i][j] = c[i][j];
}

bool sdk::render::build_view_projection(float out[16])
{
	if (!g_view.valid)
		return false;

	constexpr float near_plane = 0.05f;
	constexpr float far_plane = 1024.0f;

	const float* r = g_view.right;
	const float* u = g_view.up;
	const float* f = g_view.forward;

	// View matrix, column-major, camera at the origin. OpenGL looks down -Z,
	// hence the negated forward row.
	const float v[16] = {
		 r[0],  u[0], -f[0], 0.0f,
		 r[1],  u[1], -f[1], 0.0f,
		 r[2],  u[2], -f[2], 0.0f,
		 0.0f,  0.0f,  0.0f, 1.0f,
	};

	const float p00 = g_view.fov_x;
	const float p11 = g_view.fov_y;
	const float p22 = (far_plane + near_plane) / (near_plane - far_plane);
	const float p23 = (2.0f * far_plane * near_plane) / (near_plane - far_plane);

	// out = P * V
	for (int col = 0; col < 4; ++col)
	{
		const float vx = v[col * 4 + 0];
		const float vy = v[col * 4 + 1];
		const float vz = v[col * 4 + 2];
		const float vw = v[col * 4 + 3];

		out[col * 4 + 0] = p00 * vx;
		out[col * 4 + 1] = p11 * vy;
		out[col * 4 + 2] = p22 * vz + p23 * vw;
		out[col * 4 + 3] = -vz;
	}

	return true;
}

bool sdk::render::project_box(const double mn[3], const double mx[3],
                               float& x0, float& y0, float& x1, float& y1)
{
	if (!g_view.valid)
		return false;

	float corners[8][3];
	box_corners(mn, mx, corners);

	x0 = FLT_MAX; y0 = FLT_MAX;
	x1 = -FLT_MAX; y1 = -FLT_MAX;

	bool any = false;
	for (const auto& e : box_edges)
	{
		float pa[2], pb[2];
		if (!project_segment(corners[e[0]], corners[e[1]], pa, pb))
			continue;

		any = true;
		if (pa[0] < x0) x0 = pa[0];
		if (pa[0] > x1) x1 = pa[0];
		if (pa[1] < y0) y0 = pa[1];
		if (pa[1] > y1) y1 = pa[1];
		if (pb[0] < x0) x0 = pb[0];
		if (pb[0] > x1) x1 = pb[0];
		if (pb[1] < y0) y0 = pb[1];
		if (pb[1] > y1) y1 = pb[1];
	}

	return any;
}
