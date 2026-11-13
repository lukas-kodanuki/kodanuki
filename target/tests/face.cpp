#include "plugins/geometry/tree/face.h"
#include "check.h"
#include "plugins/geometry/tree/build.h"
#include "plugins/geometry/tree/cut.h"
#include "plugins/geometry/tree/quadric.h"
#include "plugins/transport/paths/paths.h"
#include "sdf/scene.h"
#include "syntax/evaluator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace kodanuki {

using Point = std::array<double, 3>;

struct Reading {
	std::uint32_t bits;
	bool clear;
};

struct Agreement {
	std::size_t tested;
	std::size_t wrong;
	std::size_t leaves;
};

double programValueAt(const Program& program, const Point& p) {
	const Inputs<double> inputs = {
		.arguments = nullptr,
		.p         = { p[0], p[1], p[2] },
		.n         = { 0, 0, 0 },
	};

	std::vector<double> registers;
	return evaluate(program, inputs, registers);
}

std::vector<std::shared_ptr<const Program>> sharedPrograms(const std::vector<Program>& programs) {
	std::vector<std::shared_ptr<const Program>> shared;
	for (const Program& program : programs) {
		shared.push_back(std::make_shared<const Program>(program));
	}
	return shared;
}

double unionAt(const std::vector<Program>& programs, const Point& p) {
	double nearest = 1e30;
	for (const Program& program : programs) {
		nearest = std::min(nearest, programValueAt(program, p));
	}
	return nearest;
}

math::Interval cubeSpan(double coordinate, double size) {
	const double lower = std::floor(coordinate / size) * size;
	return math::Interval {
		.lower = lower,
		.upper = lower + size,
	};
}

Box cubeAround(const Point& p, double size) {
	return Box {
		.axes = { cubeSpan(p[0], size), cubeSpan(p[1], size), cubeSpan(p[2], size) },
	};
}

Point unitToWorld(const Box& box, const Point& u) {
	return { boxCentre(box, 0) + boxHalf(box, 0) * u[0], boxCentre(box, 1) + boxHalf(box, 1) * u[1], boxCentre(box, 2) + boxHalf(box, 2) * u[2] };
}

bool isPlane(const Quadric& quadric) {
	const double* q = quadric.coefficients;
	return q[0] == 0 && q[1] == 0 && q[2] == 0 && q[3] == 0 && q[4] == 0 && q[5] == 0;
}

void checkBoxCorner() {
	const Scene scene = sceneOrExit(writeScene("corner", R"(
		function box(size, r) {
			let q = abs(p) - size / 2 + r;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0) - r;
		}

		box([1, 1, 1], 0);
	)"))
							.scene;

	const std::vector<Program> programs = objectPrograms(scene);
	const Point corner                  = { 0.45, 0.45, 0.45 };

	const std::vector<std::shared_ptr<const Program>> shared = sharedPrograms(programs);

	const Cell cell = {
		.programs  = shared,
		.box       = cubeAround(corner, 0.2),
		.tolerance = 1e-9,
	};

	const Faces faces = cellFaces(cell);
	check(!faces.crowded && faces.faces.size() == 3, "a box corner cell has three faces");
	check(faces.table[0] == 0x80, "a box corner is inside where all three planes are");
	for (const Face& face : faces.faces) {
		check(isPlane(face.quadric) && face.proven, "a box corner's faces are proven planes");
	}
}

double sampledError(const Program& program, const Face& face, const Box& box) {
	std::mt19937 random(9);
	std::uniform_real_distribution<double> unit(-1, 1);
	double worst = 0;
	for (int s = 0; s < 400; ++s) {
		const Point u = { unit(random), unit(random), unit(random) };
		worst         = std::max(worst, std::abs(programValueAt(program, unitToWorld(box, u)) - quadricValue(face.quadric, u.data())));
	}
	return worst;
}

Reading readingAt(const Faces& faces, const Point& u) {
	Reading reading = {
		.bits  = 0,
		.clear = true,
	};

	for (std::size_t j = 0; j < faces.faces.size(); ++j) {
		const double value = quadricValue(faces.faces[j].quadric, u.data());
		reading.clear      = reading.clear && std::abs(value) > faces.faces[j].bound;
		reading.bits |= value < 0 ? 1u << j : 0u;
	}
	return reading;
}

bool isLeaf(const Faces& faces) {
	bool proven = !faces.crowded && faces.faces.size() <= 5;
	for (const Face& face : faces.faces) {
		proven = proven && face.proven;
	}
	return proven;
}

void checkCellTable(Agreement& agreement, const std::vector<Program>& programs, const Box& box) {
	std::vector<Program> cut;
	for (const Program& program : programs) {
		cut.push_back(cutProgram(program, box).program);
	}

	const std::vector<std::shared_ptr<const Program>> shared = sharedPrograms(cut);

	const Cell cell = {
		.programs  = shared,
		.box       = box,
		.tolerance = 0.001,
	};

	const Faces faces = cellFaces(cell);
	if (!isLeaf(faces)) {
		return;
	}
	agreement.leaves += 1;
	for (const Face& face : faces.faces) {
		agreement.wrong += sampledError(face.program, face, box) <= face.bound ? 0 : 1;
	}
	std::mt19937 random(agreement.leaves);
	std::uniform_real_distribution<double> unit(-1, 1);
	for (int s = 0; s < 40; ++s) {
		const Point u         = { unit(random), unit(random), unit(random) };
		const Reading reading = readingAt(faces, u);
		const bool inside     = tableInside(faces, reading.bits);
		const double value    = unionAt(programs, unitToWorld(box, u));
		const bool measured   = reading.clear && std::abs(value) > 1e-9;
		agreement.tested += measured ? 1 : 0;
		agreement.wrong += measured && inside != (value < 0) ? 1 : 0;
	}
}

void checkTables(const char* path) {
	const Scene scene                   = sceneOrExit(path).scene;
	const std::vector<Program> programs = objectPrograms(scene);
	std::mt19937 random(3);
	std::uniform_int_distribution<std::size_t> pick(0, scene.leaves.size() - 1);
	std::uniform_real_distribution<double> offset(-0.4, 0.4);

	Agreement agreement = {
		.tested = 0,
		.wrong  = 0,
		.leaves = 0,
	};

	for (int s = 0; s < 600; ++s) {
		const double* origin = scene.leaves[pick(random)].placement.origin;
		const Point p        = { origin[0] + offset(random), origin[1] + offset(random), origin[2] + offset(random) };
		checkCellTable(agreement, programs, cubeAround(p, 0.0625));
		checkCellTable(agreement, programs, cubeAround(p, 0.015625));
	}
	check(agreement.leaves > 100 && agreement.tested > 1000, "many proven cells were tested");
	check(agreement.wrong == 0, "proven bounds hold and the truth table over the quadrics gives the program's sign");
}

const Scene& sphereScene() {
	static const Scene scene = sceneOrExit(writeScene("sphere", R"(
		function sphere(r) {
			return length(p) - r;
		}

		sphere(1);
	)"))
								   .scene;

	return scene;
}

const Scene& torusScene() {
	static const Scene scene = sceneOrExit(writeScene("ring", R"(
		function torus(big, small) {
			return length([length(p.xy) - big, p.z]) - small;
		}

		torus(1, 0.4);
	)"))
								   .scene;

	return scene;
}

void checkSphereExact() {
	const std::vector<std::shared_ptr<const Program>> shared = sharedPrograms(objectPrograms(sphereScene()));

	const Cell cell = {
		.programs  = shared,
		.box       = cubeAround({ 0.48, 0.6, 0.64 }, 0.5),
		.tolerance = 1e-6,
	};

	const Faces faces = cellFaces(cell);
	check(!faces.crowded && faces.faces.size() == 1 && faces.faces[0].proven && faces.faces[0].bound < 1e-9, "a sphere's face is its exact quadric, proven at any size");
}

void checkCurvedBounds() {
	const std::vector<Program> programs = objectPrograms(torusScene());
	const Point p                       = { 1.0117, 0.7588, 0.3 };
	double previous                     = 0;
	for (double size = 0.25; size > 0.01; size /= 2) {
		const std::vector<std::shared_ptr<const Program>> shared = sharedPrograms(programs);

		const Cell cell = {
			.programs  = shared,
			.box       = cubeAround(p, size),
			.tolerance = size * size * size,
		};

		const Faces faces = cellFaces(cell);
		check(!faces.crowded && faces.faces.size() == 1 && faces.table[0] == 2, "a cell on a torus has one face");
		if (faces.faces.size() != 1) {
			return;
		}
		const Face& face = faces.faces[0];
		check(face.proven, "a curved face is proven within the cube of its cell size");
		const double bound = quadricBound(face.program, face.quadric, cell.box);
		check(sampledError(face.program, face, cell.box) <= std::min(face.bound, bound), "the proven bound holds at samples");
		check(previous == 0 || (bound > previous / 12 && bound < previous / 5), "the bound shrinks like the cube of the size");
		previous = bound;
	}
}

double unitValue(const Program& program, const Box& box, const Point& u) {
	return programValueAt(program, unitToWorld(box, u));
}

void checkTaylor() {
	const Scene scene = sceneOrExit(writeScene("torus", R"(
		function torus(big, small) {
			return length([length(p.xy) - big, p.z]) - small;
		}

		turn(x, 0.5) turn(z, 0.3) torus(1, 0.3);
	)"))
							.scene;

	const Program program = objectPrograms(scene)[0];
	const Box box         = cubeAround({ 0.9, 0.5, 0.2 }, 0.2);
	const Quadric quadric = taylorQuadric(program, box);
	const double d        = 1e-4;
	const int rows[6]     = { 0, 1, 2, 0, 0, 1 };
	const int columns[6]  = { 0, 1, 2, 1, 2, 2 };
	double worst          = std::abs(quadric.coefficients[9] - unitValue(program, box, { 0, 0, 0 }));
	for (int i = 0; i < 3; ++i) {
		Point forward      = { 0, 0, 0 };
		Point backward     = { 0, 0, 0 };
		forward[i]         = d;
		backward[i]        = -d;
		const double slope = (unitValue(program, box, forward) - unitValue(program, box, backward)) / (2 * d);
		worst              = std::max(worst, std::abs(quadric.coefficients[6 + i] - slope));
	}
	for (int k = 0; k < 6; ++k) {
		Point corners[4]      = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
		const double signs[4] = { 1, -1, -1, 1 };
		double sum            = 0;
		for (int c = 0; c < 4; ++c) {
			corners[c][rows[k]] += (c & 1) != 0 ? -d : d;
			corners[c][columns[k]] += (c & 2) != 0 ? -d : d;
			sum += signs[c] * unitValue(program, box, corners[c]);
		}
		const double scale = rows[k] == columns[k] ? 8 : 4;
		worst              = std::max(worst, std::abs(quadric.coefficients[k] - sum / (scale * d * d)));
	}

	const Near near = {
		.got       = worst,
		.want      = 0,
		.tolerance = 1e-6,
		.what      = "Taylor coefficients equal finite differences",
	};

	checkNear(near);
}

double programAtPoint3(const Program& program, const Point& p) {
	std::vector<double> registers;

	const Probe probe = {
		.p         = { p[0], p[1], p[2] },
		.registers = registers,
	};

	return programDistance(program, probe);
}

void checkStacked() {
	const Scene scene = sceneOrExit(writeScene("stacked", R"(
		function box(size) {
			let q = abs(p) - size / 2;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);
		}

		paint(matte([0.1, 0.5, 0.1])) at([0, 0, -4.5]) box([40, 40, 10]);
		paint(matte([0.1, 0.1, 0.1])) at([0, 0, 0.32]) box([20, 20, 0.4]);
		paint(matte([0.5, 0.5, 0.5])) at([0, 0, 0.1]) box([2, 2, 0.2]);
	)"))
							.scene;

	const std::vector<Program> programs = objectPrograms(scene);

	const std::vector<std::shared_ptr<const Program>> shared = sharedPrograms(programs);

	const Cell cell = {
		.programs  = shared,
		.box       = cubeAround({ 5.2, 5.2, 0.5 }, 1),
		.tolerance = 1e-3,
	};

	const Faces faces = cellFaces(cell);
	const double top  = faces.faces.size() == 1 ? programAtPoint3(faces.faces[0].program, { 5.2, 5.2, 0.52 }) : 1;
	check(!faces.crowded && faces.faces.size() == 1 && std::abs(top) < 1e-9, "slabs stacked in a cell leave only the face no point of the cell can pass without, the top slab's");
}

void checkSmoothCell(const Scene& scene, const Point& p) {
	const std::vector<Program> programs = objectPrograms(scene);

	const std::vector<std::shared_ptr<const Program>> shared = sharedPrograms(programs);

	const Cell cell = {
		.programs  = shared,
		.box       = cubeAround(p, 0.02),
		.tolerance = 1e-3,
	};

	const Faces faces = cellFaces(cell);
	check(!faces.crowded && faces.faces.size() == 1 && faces.table[0] == 2, "a cell on a smooth join or a rounded corner has one face");
	if (faces.faces.size() != 1) {
		return;
	}
	const Face& face = faces.faces[0];
	check(face.proven, "the smooth face is proven");
	check(sampledError(face.program, face, cell.box) <= face.bound, "the smooth face's bound holds at samples");
}

void checkSmoothJoin() {
	const Scene scene = sceneOrExit(writeScene("blend", R"(
		function sphere(r) {
			return length(p) - r;
		}

		merge(0.3) {
			at([-0.4, 0, 0]) sphere(0.5);
			at([0.4, 0, 0]) sphere(0.5);
		}

	)"))
							.scene;

	checkSmoothCell(scene, { 0.01, 0.41, 0.01 });
	checkSmoothCell(scene, { -0.24375, 0.475, 0.001 });
}

void checkRoundedCorner() {
	const Scene scene = sceneOrExit(writeScene("rounded", R"(
		function box(size, r) {
			let q = abs(p) - size / 2 + r;
			return length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0) - r;
		}

		box([1.01, 1.01, 1.01], 0.2);
	)"))
							.scene;

	checkSmoothCell(scene, { 0.4464, 0.4464, 0.31 });
	checkSmoothCell(scene, { 0.31, 0.45, 0.45 });
}

double insideOf(std::mt19937& random, const Box& box, int axis) {
	std::uniform_real_distribution<double> along(box.axes[axis].lower, box.axes[axis].upper);
	return along(random);
}

double ringValue(const Ring& ring, const Box& box, const Point& p) {
	double v[3] = { ring.origin[0], ring.origin[1], ring.origin[2] };
	for (int axis = 0; axis < 3; ++axis) {
		const double u = (p[std::size_t(axis)] - box.axes[axis].lower) / (box.axes[axis].upper - box.axes[axis].lower);
		for (int k = 0; k < 3; ++k) {
			v[k] += ring.axes[axis][k] * u;
		}
	}
	const double around = v[0] * v[0] + v[1] * v[1];
	const double sum    = around + v[2] * v[2] + 1 - ring.minor * ring.minor;
	const double value  = sum * sum - 4 * around;
	return ring.turned ? -value : value;
}

int ringDisagreements(const char* statement, const Box& box) {
	const std::string functions = "function torus(big, small) {\n\treturn length([length(p.xy) - big, p.z]) - small;\n}\n\nfunction box(size) {\n\tlet q = abs(p) - size / 2;\n\treturn length(max(q, 0)) + min(max(q.x, max(q.y, q.z)), 0);\n}\n\n";
	const std::string text      = functions + statement + "\n";
	const Scene scene           = sceneOrExit(writeScene("ring", text.c_str())).scene;

	const std::vector<std::shared_ptr<const Program>> shared = sharedPrograms(scene.objectPrograms);

	const Cell cell = {
		.programs  = shared,
		.box       = box,
		.tolerance = 1e-6,
	};

	const Faces faces = cellFaces(cell);
	if (faces.crowded || faces.faces.size() != 1 || !faces.faces[0].ring) {
		return -1;
	}
	std::mt19937 random(9);
	int wrong = 0;
	for (int k = 0; k < 2000; ++k) {
		const Point p      = { insideOf(random, box, 0), insideOf(random, box, 1), insideOf(random, box, 2) };
		const double field = programValueAt(scene.objectPrograms[0], p);
		const bool inside  = (faces.table[0] & 2u) != 0 ? ringValue(*faces.faces[0].ring, box, p) <= 0 : ringValue(*faces.faces[0].ring, box, p) > 0;
		wrong += std::abs(field) > 1e-9 && inside != (field < 0) ? 1 : 0;
	}
	return wrong;
}

void checkRingFace() {
	const Box box     = cubeAround({ 0.3, -0.2, 0.1 }, 1.6);
	const int turned  = ringDisagreements("at([0.3, -0.2, 0.1]) turn(x, 0.7) turn(z, 0.4) torus(1, 0.4);", box);
	const int scaled  = ringDisagreements("at([0.3, -0.2, 0.1]) turn(x, 0.7) scale([1.3, 0.8, 1.1]) torus(1, 0.4);", box);
	const int cutAway = ringDisagreements("cut { at([0.3, -0.2, 0.1]) box([5, 5, 5]); at([0.3, -0.2, 0.1]) torus(1, 0.4); }", cubeAround({ 1.7, -0.2, 0.1 }, 0.5));
	std::printf("faces ring: %d, %d and %d of 2000 points read the wrong side of a turned, a stretched and a cut away ring\n", turned, scaled, cutAway);
	check(turned == 0 && scaled == 0 && cutAway == 0, "a torus under a turn or a stretch, or cut away, is one ring face, exact over a large cell");
}

void checkFaces() {
	checkBoxCorner();
	checkSmoothJoin();
	checkRoundedCorner();
	checkStacked();
	checkSphereExact();
	checkRingFace();
	checkCurvedBounds();
	checkTaylor();
	checkTables("scenes/magic-cube.sdf");
	checkTables("scenes/chess-board.sdf");
}

}
