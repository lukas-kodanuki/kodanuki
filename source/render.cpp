#include "core/render.h"
#include "core/options.h"

#include <cstdio>
#include <utility>

using namespace kodanuki;

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: kodanuki scene.sdf [--size w h] [--seed n] [--tolerance t] [--samples n] [--batch n] [--split n] [--surfaces n] [--collisions n] [--threads n] [--use key=value ...] [--use source=scene] [--use sink=file|frames] [--use observer=progress+metrics|none] [--region lx ly lz hx hy hz] [--least e] [--depth n] [--time t] --out image.ppm|directory [--pfm image.pfm] [--reconstruction mean|temporal|temporal+denoise|temporal+denoise+upscale] [--factor 1|2|3|4]\n");
		return 1;
	}

	const CommandLine arguments = {
		.count  = argc,
		.values = argv,
	};

	SceneFile file        = sceneAtOrExit(argv[1], timeArgument(arguments));
	const Options options = readOptions(file, arguments, false);
	return render(std::move(file), options);
}
