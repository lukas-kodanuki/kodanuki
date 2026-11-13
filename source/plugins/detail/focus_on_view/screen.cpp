#include "plugins/detail/focus_on_view/screen.h"

namespace kodanuki {

constexpr double unseenTolerance = 1;
constexpr double frameMargin     = 4;

struct Beyond {
	int behind;
	int sides[4];
};

Screen frameScreen(const View& view) {
	return Screen {
		.view   = view,
		.unseen = view.open || !(view.wide > 0) ? 0 : unseenTolerance,
		.margin = frameMargin,
	};
}

Screen emptyScreen() {
	const View none = {
		.eye     = { 0, 0, 0 },
		.forward = { 0, 0, 1 },
		.right   = { 1, 0, 0 },
		.up      = { 0, 1, 0 },
		.spread  = 0,
		.corner  = 1,
		.wide    = 0,
		.tall    = 0,
		.open    = false,
	};

	return Screen {
		.view   = none,
		.unseen = 0,
		.margin = 0,
	};
}

double cornerAlong(const Box& box, int corner, int axis) {
	return ((corner >> axis) & 1) != 0 ? box.axes[axis].upper : box.axes[axis].lower;
}

math::Vector cornerApart(const Box& box, const math::Vector& eye, int corner) {
	const math::Vector place = { cornerAlong(box, corner, 0), cornerAlong(box, corner, 1), cornerAlong(box, corner, 2) };
	return place - eye;
}

void countCorner(Beyond& beyond, const Screen& screen, const math::Vector& apart) {
	const double wide = screen.view.wide + screen.margin * screen.view.spread;
	const double tall = screen.view.tall + screen.margin * screen.view.spread;
	const double x    = math::dot(screen.view.right, apart);
	const double y    = math::dot(screen.view.up, apart);
	const double z    = math::dot(screen.view.forward, apart);
	beyond.behind += z <= 0 ? 1 : 0;
	beyond.sides[0] += x > z * wide ? 1 : 0;
	beyond.sides[1] += -x > z * wide ? 1 : 0;
	beyond.sides[2] += y > z * tall ? 1 : 0;
	beyond.sides[3] += -y > z * tall ? 1 : 0;
}

bool cellBeyond(const Screen& screen, const Box& box) {
	if (!(screen.unseen > 0)) {
		return false;
	}

	Beyond beyond = {
		.behind = 0,
		.sides  = { 0, 0, 0, 0 },
	};

	for (int corner = 0; corner < 8; ++corner) {
		countCorner(beyond, screen, cornerApart(box, screen.view.eye, corner));
	}
	return beyond.behind == 8 || beyond.sides[0] == 8 || beyond.sides[1] == 8 || beyond.sides[2] == 8 || beyond.sides[3] == 8;
}

double focusOnViewShare(const Screen& screen, const Box& box) {
	return cellBeyond(screen, box) ? screen.unseen : 0;
}

}
