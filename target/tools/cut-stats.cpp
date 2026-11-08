#include "frame/run.h"
#include "sdf/scene.h"
#include "spots.h"
#include "syntax/evaluator.h"
#include "tree/cut.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace kodanuki;

struct Totals {
	std::size_t cells;
	std::size_t surfaces;
	std::size_t steps;
};

void cutChain(std::vector<Totals>& totals, const Program& whole, const Spot& p) {
	const double sizes[3] = { 1, 0.25, 0.0625 };
	Program parent        = whole;
	for (int k = 0; k < 3; ++k) {
		const Cut cut     = cutProgram(parent, cellAround(p, sizes[k]));
		const bool inside = cut.occupancy == Occupancy::Surface;
		totals[k].surfaces += inside ? 1 : 0;
		totals[k].steps += inside ? cut.program.steps.size() : 0;
		parent = cut.program;
	}
}

void printWhole(const std::vector<Program>& programs) {
	std::size_t sum     = 0;
	std::size_t largest = 0;
	for (const Program& program : programs) {
		sum += program.steps.size();
		largest = std::max(largest, program.steps.size());
	}
	std::printf("%zu object programs: %zu steps in all, %.1f on average, %zu at most\n", programs.size(), sum, double(sum) / double(programs.size()), largest);
}

void printLevel(const Totals& totals, double size) {
	const double cells    = double(totals.cells);
	const double surfaces = double(totals.surfaces);
	std::printf("cell %-6g: %.2f objects left per cell, %.1f steps per object left, %.1f steps per cell\n", size, surfaces / cells, double(totals.steps) / surfaces, double(totals.steps) / cells);
}

int main(int argc, char** argv) {
	const SceneFile file          = sceneOrExit(argc > 1 ? argv[1] : "scenes/chess-board.sdf");
	const std::vector<Spot> spots = surfaceSpots(file, 300, 1);

	const Totals none = {
		.cells    = spots.size(),
		.surfaces = 0,
		.steps    = 0,
	};

	std::vector<Totals> totals(3, none);
	for (const Spot& p : spots) {
		for (const Program& program : file.scene.objectPrograms) {
			cutChain(totals, program, p);
		}
	}
	printWhole(file.scene.objectPrograms);
	printLevel(totals[0], 1);
	printLevel(totals[1], 0.25);
	printLevel(totals[2], 0.0625);
	return 0;
}
