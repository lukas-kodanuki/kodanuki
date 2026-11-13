#pragma once

#include "core/frame.h"
#include "plugins/geometry/tree/detail.h"
#include "plugins/geometry/tree/pass.h"
#include "plugins/geometry/tree/tree.h"
#include "plugins/transport/paths/paths.h"
#include "sdf/scene.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace kodanuki {

struct Near {
	double got;
	double want;
	double tolerance;
	const char* what;
};

int& failures();
void check(bool passed, const char* what);
void checkNear(const Near& near);

struct TestTree {
	Detail detail;
	Tree tree;
};

const char* writeScene(const char* name, const char* text);
Detail testDetail(const SceneFile& file, const Region& region, const std::vector<DetailMember>& members);
TestTree testTree(const SceneFile& file, int workers);
TestTree passedTestTree(const SceneFile& file, const std::vector<PassMember>& passes, int workers);
std::vector<PassMember> passesWithoutFaceSlabs();

struct Renderer;

struct FrameInput {
	const Geometry& geometry;
	const Looks& looks;
	const Lighting& lighting;
	const Shot& shot;
	const Lens& lens;
	const Plugin& transport;
	const void* transportWorld;
	const std::vector<Plugin>& preparations;
	const Plugin& stopping;
	int perFrame;
	std::string execution;
	int threads;
	const Observing& observing;
};

struct Framed {
	Image image;
	History history;
	double samples;
	int rounds;
};

Renderer testRenderer(const FrameInput& input, const FrameStep& step);
Framed framedBy(const FrameInput& input, const FrameStep& step, const std::vector<Plugin>& reconstruction, History history);
Framed stillFrame(const FrameInput& input);

struct ChosenMembers {
	Sampling sampling;
	Strategies strategies;
};

ChosenMembers coreChosen();
ChosenMembers defaultChosen();
const Plugin& testPlugin(const std::string& name);
std::vector<Plugin> testPlugins(const std::vector<std::string>& names);
std::vector<Plugin> testPreparations(const std::vector<std::string>& names);
std::vector<std::string> defaultPreparations();

}
