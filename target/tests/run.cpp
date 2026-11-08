#include "frame/run.h"
#include "check.h"

#include <string>
#include <vector>

namespace kodanuki {

void checkProgressLine() {
	const Snapshot snapshot = {
		.stage    = Stage::Frame,
		.finished = 30,
		.total    = 120,
		.frame    = 0,
		.frames   = 0,
		.staged   = 12,
		.drawing  = 10,
		.left     = 30,
	};

	check(progressLine(snapshot) == "render [######..................] 30/120 tiles  25% 10.0 s, about 30.0 s left", "the progress line shows the tiles, the share, the time so far and the time left");
}

void checkRun() {
	checkProgressLine();
}

}
