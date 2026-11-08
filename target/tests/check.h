#pragma once

#include "sdf/scene.h"
#include "tree/detail.h"
#include "tree/pass.h"
#include "tree/tree.h"

#include <cmath>
#include <cstdio>
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

}
