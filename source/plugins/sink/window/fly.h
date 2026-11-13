#pragma once

#include "sdf/scene.h"

namespace kodanuki::viewer {

/**
 * What the person steers the camera with since the last frame of the
 * window.
 */
struct Steering {
	/**
	 * W is held: move along the view.
	 */
	bool forward;
	/**
	 * S is held: move against the view.
	 */
	bool back;
	/**
	 * A is held: move to the left of the view.
	 */
	bool left;
	/**
	 * D is held: move to the right of the view.
	 */
	bool right;
	/**
	 * Space is held: move along the camera's up.
	 */
	bool rise;
	/**
	 * Ctrl is held: move against the camera's up.
	 */
	bool sink;
	/**
	 * Shift is held: move four times as fast.
	 */
	bool fast;
	/**
	 * How far the mouse moved to the right while its right button was
	 * held, in pixels.
	 */
	float turnX;
	/**
	 * How far the mouse moved down while its right button was held, in
	 * pixels.
	 */
	float turnY;
	/**
	 * How far the wheel turned, positive away from the person.
	 */
	float wheel;
};

/**
 * A camera flown by the person, and how fast it flies.
 */
struct Fly {
	/**
	 * The camera; the eye and the target move together, so the target
	 * keeps its distance and the focus through it holds.
	 */
	Camera camera;
	/**
	 * How far the camera flies in a second, in scene units.
	 */
	float speed;
};

/**
 * Return no steering: nothing held, nothing moved.
 */
Steering stillSteering();

/**
 * Return whether a steering moves or turns the camera.
 *
 * @param steering The steering.
 */
bool steers(const Steering& steering);

/**
 * Return a fly from a scene's camera, flying the distance from the eye
 * to the target in two seconds.
 *
 * @param camera The scene's camera.
 */
Fly startedFly(const Camera& camera);

/**
 * Return the fly after a time of steering.
 *
 * The keys move the eye and the target together, along the view, its
 * right and the camera's up, at the fly's speed (four times with Shift).
 * The mouse turns the view about the eye: right turns about the up,
 * down tilts it, 0.003 radians a pixel, never closer than a degree to
 * the up. Each step of the wheel scales the speed by 1.25.
 *
 * @param fly The fly before.
 * @param steering What the person did.
 * @param seconds How long the steering lasted.
 */
Fly flown(const Fly& fly, const Steering& steering, double seconds);

}
