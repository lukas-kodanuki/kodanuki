#include "core/options.h"
#include "math/vector.h"
#include "sdf/scene.h"
#include "syntax/evaluator.h"

#include <cstdio>
#include <fstream>
#include <vector>

using namespace kodanuki;

int main(int argc, char** argv) {
	if (argc != 3) {
		std::fprintf(stderr, "usage: kodanuki_probe scene.sdf points.txt\n");
		return 1;
	}
	const SceneFile file = sceneOrExit(argv[1]);
	std::ifstream in(argv[2]);
	if (!in) {
		std::fprintf(stderr, "cannot read %s\n", argv[2]);
		return 1;
	}
	std::vector<double> registers;
	math::Vector p = { 0, 0, 0 };
	while (in >> p.x >> p.y >> p.z) {
		const Probe probe = {
			.p         = p,
			.registers = registers,
		};

		std::printf("%.9g\n", sceneDistance(file.scene, probe));
	}
	return 0;
}
