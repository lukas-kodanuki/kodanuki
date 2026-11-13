#include "core/options.h"
#include "core/render.h"

#include <cstdio>
#include <utility>

using namespace kodanuki;

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: kodanuki_view scene.sdf [--size w h] [--seed n] [--tolerance t] [--samples n] [--batch n] [--split n] [--surfaces n] [--collisions n] [--threads n] [--use key=value ...] [--use source=scene+flight] [--use sink=window+frames] [--use observer=progress+metrics|none] [--region lx ly lz hx hy hz] [--least e] [--depth n] [--internal w h] [--time t] [--out directory]\n");
		return 1;
	}

	const CommandLine arguments = {
		.count  = argc,
		.values = argv,
	};

	SceneFile file        = sceneAtOrExit(argv[1], timeArgument(arguments));
	const Options options = readOptions(file, arguments, true);
	return render(std::move(file), options);
}
