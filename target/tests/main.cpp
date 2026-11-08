#include "check.h"

#include <cstdlib>
#include <fstream>
#include <string>

namespace kodanuki {

void checkDenoise();
void checkExecution();
void checkFaces();
void checkLanguage();
void checkLight();
void checkNumbers();
void checkReconstruction();
void checkRun();
void checkSampler();
void checkSequence();
void checkShading();
void checkSpectral();
void checkStopping();
void checkTransport();
void checkTree();
void checkUpscale();
void checkViewer();
void checkWalk();
void checkWeight();

int& failures() {
	static int count = 0;
	return count;
}

void check(bool passed, const char* what) {
	if (!passed) {
		std::printf("FAIL %s\n", what);
		failures() += 1;
	}
}

void checkNear(const Near& near) {
	if (!(std::abs(near.got - near.want) <= near.tolerance)) {
		std::printf("FAIL %s: got %.9g, want %.9g\n", near.what, near.got, near.want);
		failures() += 1;
	}
}

const char* writeScene(const char* name, const char* text) {
	static std::string path;
	const char* directory = std::getenv("TMPDIR");
	path                  = std::string(directory ? directory : "/tmp") + "/kodanuki_" + name + ".sdf";
	std::ofstream(path) << text;
	return path.c_str();
}

}

int main() {
	kodanuki::checkLanguage();
	kodanuki::checkNumbers();
	kodanuki::checkTree();
	kodanuki::checkFaces();
	kodanuki::checkWalk();
	kodanuki::checkSpectral();
	kodanuki::checkShading();
	kodanuki::checkLight();
	kodanuki::checkWeight();
	kodanuki::checkTransport();
	kodanuki::checkExecution();
	kodanuki::checkSampler();
	kodanuki::checkStopping();
	kodanuki::checkSequence();
	kodanuki::checkReconstruction();
	kodanuki::checkDenoise();
	kodanuki::checkUpscale();
	kodanuki::checkRun();
	kodanuki::checkViewer();
	if (kodanuki::failures() == 0) {
		std::printf("all checks passed\n");
	}
	return kodanuki::failures() == 0 ? 0 : 1;
}
