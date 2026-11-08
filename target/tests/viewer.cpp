#include "viewer/fly.h"

#include "check.h"
#include "frame/shown.h"
#include "math/vector.h"

#include <cmath>
#include <cstring>

namespace kodanuki {

Camera flightCamera() {
	const Camera camera = {
		.eye      = { 0, 1, 5 },
		.target   = { 0, 1, 1 },
		.up       = { 0, 1, 0 },
		.fov      = 1,
		.aperture = 0,
		.focus    = 0,
	};

	return camera;
}

double eyeToTarget(const Camera& camera) {
	return math::length(camera.target - camera.eye);
}

math::Vector viewDirection(const Camera& camera) {
	return math::normalized(camera.target - camera.eye);
}

void checkForwardFlight() {
	const viewer::Fly start   = viewer::startedFly(flightCamera());
	viewer::Steering steering = viewer::stillSteering();
	steering.forward          = true;
	const viewer::Fly moved   = viewer::flown(start, steering, 0.5);
	steering.fast             = true;
	const viewer::Fly fast    = viewer::flown(start, steering, 0.5);

	const Near near = {
		.got       = moved.camera.eye.z,
		.want      = 4,
		.tolerance = 1e-5,
		.what      = "W flies the eye along the view at the speed, the distance to the target in two seconds",
	};

	checkNear(near);
	check(std::abs(eyeToTarget(moved.camera) - 4) < 1e-5 && std::abs(moved.camera.target.z - 0) < 1e-5, "the target moves with the eye and keeps its distance");
	check(std::abs(fast.camera.eye.z - 1) < 1e-5, "Shift flies four times as fast");
}

void checkTurns() {
	const viewer::Fly start  = viewer::startedFly(flightCamera());
	viewer::Steering right   = viewer::stillSteering();
	right.turnX              = 100;
	const viewer::Fly turned = viewer::flown(start, right, 0.1);
	const math::Vector side  = math::cross(viewDirection(start.camera), start.camera.up);
	viewer::Steering up      = viewer::stillSteering();
	up.turnY                 = -100000;
	const viewer::Fly tilted = viewer::flown(start, up, 0.1);
	const double angle       = std::acos(math::dot(viewDirection(tilted.camera), start.camera.up));

	check(math::dot(viewDirection(turned.camera), side) > 0.2 && std::abs(eyeToTarget(turned.camera) - 4) < 1e-5, "the mouse moved right turns the view to the camera's right, the target at its distance");
	check(std::memcmp(&turned.camera.eye, &start.camera.eye, sizeof(start.camera.eye)) == 0, "turning keeps the eye where it stands");
	check(viewDirection(tilted.camera).y > 0.99 && angle > 0.0174 && angle < 0.0176, "the mouse moved up tilts the view up, but stops a degree short of the up");
}

void checkWheelAndStillness() {
	const viewer::Fly start  = viewer::startedFly(flightCamera());
	viewer::Steering wheel   = viewer::stillSteering();
	wheel.wheel              = 2;
	const viewer::Fly faster = viewer::flown(start, wheel, 0.1);
	const viewer::Fly still  = viewer::flown(start, viewer::stillSteering(), 0.1);

	check(std::abs(faster.speed - start.speed * 1.5625f) < 1e-5 && viewer::steers(wheel), "two steps of the wheel scale the speed by 1.25 twice");
	check(std::memcmp(&still.camera, &start.camera, sizeof(Camera)) == 0 && still.speed == start.speed && !viewer::steers(viewer::stillSteering()), "no steering leaves the camera and the speed as they were");
}

void checkViewer() {
	checkForwardFlight();
	checkTurns();
	checkWheelAndStillness();
}

}
