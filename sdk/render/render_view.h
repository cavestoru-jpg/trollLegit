#pragma once

#include <sdk/includes.h>

// Single source of truth for world -> screen projection.
//
// Before this existed the same view-matrix code was pasted into esp.cpp,
// storage_esp.cpp, backtrack.cpp and target_esp.cpp, and the copies had
// drifted apart: target_esp's basis was the horizontal mirror of the others
// with pitch inverted on top, so its boxes never landed on the target. Every
// consumer now shares the implementation below.
namespace sdk
{
	namespace render
	{
		struct view_t
		{
			double cam_x, cam_y, cam_z;
			float  yaw, pitch, fov;

			// Orthonormal camera basis in Minecraft world space.
			float right[3];
			float up[3];
			float forward[3];

			float fov_x, fov_y;     // 1 / tan(fov/2), x additionally divided by aspect
			float half_w, half_h;

			bool valid;
		};

		// JNIEnv belonging to the calling thread. The client caches one env for
		// its worker thread, and a JNIEnv is thread-local — using the worker's
		// env from the render thread is undefined behaviour and was the reason
		// the render path could not read the camera itself.
		JNIEnv* current_thread_env();

		// Reads MinecraftClient's live camera with the calling thread's env, so
		// this is safe to call from inside the wglSwapBuffers hook. Returns
		// false and leaves the outputs untouched when the game is not ready.
		bool sample_camera(double& x, double& y, double& z,
		                   float& yaw, float& pitch, float& fov);

		// Builds this frame's view. Call once per frame before projecting.
		void set_view(double cam_x, double cam_y, double cam_z,
		              float yaw, float pitch, float fov,
		              int screen_w, int screen_h);

		const view_t& view();

		// Marks the view unusable. Must be called on any frame where the camera
		// could not be sampled, otherwise consumers silently reuse the previous
		// frame's camera.
		void invalidate_view();

		// World point -> view space. pz is depth along the view axis.
		void to_view(float x, float y, float z, float& px, float& py, float& pz);

		// Perspective divide. False when the point is at or behind the near plane.
		bool world_to_screen(float x, float y, float z, float& sx, float& sy);

		// Projects a world-space segment, clipping against the near plane so an
		// edge with one endpoint behind the camera still draws its visible part.
		// False only when the whole segment is behind the camera.
		bool project_segment(const float a[3], const float b[3],
		                     float out_a[2], float out_b[2]);

		// Screen-space bounds of a world AABB. Walks the 12 edges with near-plane
		// clipping, so the result stays correct when the box straddles the camera
		// plane — taking min/max over only the corners that survived the divide
		// (what the old code did) shrinks the box or drops it entirely up close.
		bool project_box(const double mn[3], const double mx[3],
		                 float& x0, float& y0, float& x1, float& y1);

		// The 12 edges of an AABB as index pairs into the corner order produced
		// by box_corners().
		extern const int box_edges[12][2];
		void box_corners(const double mn[3], const double mx[3], float out[8][3]);

		// Column-major view-projection for GPU submission, with the camera at
		// the origin — vertices must therefore be camera-relative. Near matches
		// Minecraft's 0.05 so depth values line up with the game's depth buffer;
		// far barely moves the mapping once near is that small.
		// Returns false when the view is not valid.
		bool build_view_projection(float out[16]);
	}
}
