#include "frame/run.h"
#include "sdf/scene.h"
#include "spots.h"
#include "syntax/evaluator.h"
#include "tree/cut.h"
#include "tree/face.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace kodanuki;

struct Totals {
	std::size_t cells;
	std::size_t candidates;
	std::size_t faces;
	std::size_t crowded;
	std::size_t many;
	std::size_t leaves;
	std::size_t proven;
	std::size_t counted;
	double bounds;
	double seconds;
};

void addFaces(Totals& totals, const Faces& faces) {
	totals.cells += 1;
	totals.candidates += faces.candidates;
	totals.crowded += faces.crowded ? 1 : 0;
	if (faces.crowded) {
		return;
	}
	totals.counted += 1;
	totals.faces += faces.faces.size();
	totals.many += faces.faces.size() > 5 ? 1 : 0;
	bool all = faces.faces.size() <= 5;
	for (const Face& face : faces.faces) {
		all = all && face.proven;
		totals.proven += face.proven ? 1 : 0;
		totals.bounds += face.proven ? face.bound : 0;
	}
	totals.leaves += all ? 1 : 0;
}

std::vector<Program> surfacePrograms(const std::vector<Program>& parents, const Box& box) {
	std::vector<Program> cut;
	for (const Program& parent : parents) {
		cut.push_back(cutProgram(parent, box).program);
	}
	return cut;
}

std::vector<std::shared_ptr<const Program>> sharedPrograms(const std::vector<Program>& programs) {
	std::vector<std::shared_ptr<const Program>> shared;
	for (const Program& program : programs) {
		shared.push_back(std::make_shared<const Program>(program));
	}
	return shared;
}

void cellChain(std::vector<Totals>& totals, const std::vector<Program>& programs, const Spot& p) {
	const double sizes[3]   = { 0.25, 0.0625, 0.015625 };
	std::vector<Program> at = programs;
	for (int k = 0; k < 3; ++k) {
		const Box box = cellAround(p, sizes[k]);
		at            = surfacePrograms(at, box);

		const std::vector<std::shared_ptr<const Program>> shared = sharedPrograms(at);

		const Cell cell = {
			.programs  = shared,
			.box       = box,
			.tolerance = 0.001,
		};

		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		addFaces(totals[k], cellFaces(cell));
		totals[k].seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}
}

void printLevel(const Totals& t, double size) {
	const double cells   = double(t.cells);
	const double counted = double(t.counted);
	std::printf("cell %-9g: candidates %5.2f, faces after depends %5.2f, crowded %5.1f%%, over 5 faces %5.1f%%, leaf-ready (<=5, all proven at 0.001) %5.1f%%, faces proven %5.1f%%, mean proven bound %.2e, %.2f ms per cell\n", size, double(t.candidates) / cells, double(t.faces) / counted, 100 * double(t.crowded) / cells, 100 * double(t.many) / cells, 100 * double(t.leaves) / cells, 100 * double(t.proven) / double(t.faces), t.bounds / double(t.proven), 1000 * t.seconds / cells);
}

int main(int argc, char** argv) {
	const SceneFile file          = sceneOrExit(argc > 1 ? argv[1] : "scenes/chess-board.sdf");
	const std::vector<Spot> spots = surfaceSpots(file, argc > 2 ? std::atoi(argv[2]) : 300, 2.5);

	const Totals none = {
		.cells      = 0,
		.candidates = 0,
		.faces      = 0,
		.crowded    = 0,
		.many       = 0,
		.leaves     = 0,
		.proven     = 0,
		.counted    = 0,
		.bounds     = 0,
		.seconds    = 0,
	};

	std::vector<Totals> totals(3, none);
	for (const Spot& p : spots) {
		cellChain(totals, file.scene.objectPrograms, p);
	}
	printLevel(totals[0], 0.25);
	printLevel(totals[1], 0.0625);
	printLevel(totals[2], 0.015625);
	return 0;
}
