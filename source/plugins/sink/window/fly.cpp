#include "plugins/sink/window/fly.h"

#include "math/vector.h"

#include <algorithm>
#include <cmath>

namespace kodanuki::viewer {

constexpr double turnPerPixel  = 0.003;
constexpr double steepestPitch = math::pi / 2 - math::pi / 180;
constexpr double fastFactor    = 4;
constexpr double wheelStep     = 1.25;

struct Axes {
	math::Vector forward;
	math::Vector right;
	math::Vector up;
};

Steering stillSteering() {
	const Steering steering = {
		.forward = false,
		.back    = false,
		.left    = false,
		.right   = false,
		.rise    = false,
		.sink    = false,
		.fast    = false,
		.turnX   = 0,
		.turnY   = 0,
		.wheel   = 0,
	};

	return steering;
}

bool moves(const Steering& steering) {
	return steering.forward || steering.back || steering.left || steering.right || steering.rise || steering.sink;
}

bool turns(const Steering& steering) {
	return steering.turnX != 0 || steering.turnY != 0;
}

bool steers(const Steering& steering) {
	return moves(steering) || turns(steering) || steering.wheel != 0;
}

Fly startedFly(const Camera& camera) {
	const double distance = math::length(camera.target - camera.eye);
	const Fly fly         = {
		.camera = camera,
		.speed  = float(distance / 2),
	};
	return fly;
}

Axes axesOf(const Camera& camera) {
	const math::Vector forward = math::normalized(camera.target - camera.eye);
	const math::Vector up      = math::normalized(camera.up);

	const Axes axes = {
		.forward = forward,
		.right   = math::normalized(math::cross(forward, up)),
		.up      = up,
	};

	return axes;
}

Camera cameraAt(const Camera& camera, const math::Vector& eye, const math::Vector& target) {
	const Camera moved = {
		.eye      = eye,
		.target   = target,
		.up       = camera.up,
		.fov      = camera.fov,
		.aperture = camera.aperture,
		.focus    = camera.focus,
	};

	return moved;
}

math::Vector horizontalOf(const math::Vector& forward, const math::Vector& up) {
	const math::Vector horizontal = math::normalized(forward - up * math::dot(forward, up));
	if (math::length(horizontal) == 0) {
		return math::basisAround(up).tangent;
	}
	return horizontal;
}

math::Vector turnedForward(const Axes& axes, const Steering& steering) {
	const math::Vector horizontal = horizontalOf(axes.forward, axes.up);
	const math::Vector side       = math::cross(horizontal, axes.up);
	const double yaw              = steering.turnX * turnPerPixel;
	const double pitch            = std::asin(std::clamp(math::dot(axes.forward, axes.up), -1.0, 1.0)) - steering.turnY * turnPerPixel;
	const double clamped          = std::clamp(pitch, -steepestPitch, steepestPitch);
	const math::Vector heading    = horizontal * std::cos(yaw) + side * std::sin(yaw);
	return heading * std::cos(clamped) + axes.up * std::sin(clamped);
}

Camera turnedCamera(const Camera& camera, const Steering& steering) {
	if (!turns(steering)) {
		return camera;
	}

	const double distance      = math::length(camera.target - camera.eye);
	const math::Vector forward = turnedForward(axesOf(camera), steering);
	return cameraAt(camera, camera.eye, camera.eye + forward * distance);
}

math::Vector heldDirection(const Axes& axes, const Steering& steering) {
	const math::Vector along  = axes.forward * (double(steering.forward) - double(steering.back));
	const math::Vector across = axes.right * (double(steering.right) - double(steering.left));
	const math::Vector upward = axes.up * (double(steering.rise) - double(steering.sink));
	return math::normalized(along + across + upward);
}

Camera movedCamera(const Fly& fly, const Steering& steering, double seconds) {
	if (!moves(steering)) {
		return fly.camera;
	}

	const double reach      = fly.speed * seconds * (steering.fast ? fastFactor : 1);
	const math::Vector step = heldDirection(axesOf(fly.camera), steering) * reach;
	return cameraAt(fly.camera, fly.camera.eye + step, fly.camera.target + step);
}

Fly flown(const Fly& fly, const Steering& steering, double seconds) {
	const Fly turned = {
		.camera = turnedCamera(fly.camera, steering),
		.speed  = fly.speed,
	};

	const Fly result = {
		.camera = movedCamera(turned, steering, seconds),
		.speed  = float(fly.speed * std::pow(wheelStep, steering.wheel)),
	};

	return result;
}

}
