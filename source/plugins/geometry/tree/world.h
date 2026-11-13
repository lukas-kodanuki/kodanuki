#pragma once

#include "core/options.h"
#include "plugins/geometry/tree/detail.h"
#include "plugins/geometry/tree/tree.h"
#include "plugins/transport/paths/paths.h"
#include "syntax/evaluator.h"

namespace kodanuki {

/**
 * The tree of a run and the detail it was built to.
 */
struct BuiltGeometry {
	/**
	 * How fine the tree's cells were asked to be.
	 */
	Detail detail;
	/**
	 * The tree.
	 */
	Tree tree;
};

/**
 * Build the tree a scene is drawn with, flying the pilot first where
 * the Detail members name it.
 *
 * The build frees far more than the tree keeps, spread over the
 * threads' heaps; with glibc that memory is then handed back to the
 * system (malloc_trim), so that the frame's buffers do not come on top
 * of it.
 *
 * @param file The scene.
 * @param choices The members and settings.
 * @param paths What `transport/paths` reads of the scene: the features
 *        and the smooth glass the pilot's paths fly with.
 */
BuiltGeometry builtGeometry(const SceneFile& file, const Choices& choices, const PathsWorld& paths);

}
