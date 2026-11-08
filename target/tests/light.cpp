#include "light/light.h"
#include "check.h"
#include "frame/run.h"
#include "math/random.h"
#include "spectral/colour.h"
#include "syntax/evaluator.h"
#include "transport/feature.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <string>

namespace kodanuki {

struct LampCase {
	const char* name;
	Lamp lamp;
	double tolerance;
};

Lamp testLamp(LightKind kind) {
	return Lamp {
		.kind      = kind,
		.at        = { 0, 0, 0 },
		.direction = { 0, 0, 1 },
		.along     = { 0, 0, 0 },
		.across    = { 0, 0, 0 },
		.facing    = { 0, 0, 1 },
		.radius    = 0,
		.cosine    = 1,
		.power     = { 1, 1, 1 },
		.radiance  = { 1, 1, 1 },
		.area      = 0,
		.emitted   = 0,
		.edges     = quadEdgesOf({ 1, 0, 0 }, { 0, 1, 0 }),
	};
}

Lamp sphereLamp() {
	Lamp lamp     = testLamp(LightKind::Sphere);
	lamp.at       = { 0.4, -0.2, 2 };
	lamp.radius   = 0.5;
	lamp.power    = { 2, 2, 2 };
	lamp.radiance = lamp.power * (1 / (math::pi * 0.25));
	lamp.area     = 4 * math::pi * 0.25;
	lamp.emitted  = 8 * math::pi;
	return lamp;
}

Lamp quadLamp(bool square) {
	Lamp lamp     = testLamp(LightKind::Quad);
	lamp.at       = { -0.5, -0.5, 1.5 };
	lamp.along    = { 1, 0, 0 };
	lamp.across   = square ? math::Vector { 0, -1, 0 } : math::Vector { 0.4, -1, 0 };
	lamp.facing   = math::normalized(math::cross(lamp.along, lamp.across));
	lamp.area     = math::length(math::cross(lamp.along, lamp.across));
	lamp.edges    = quadEdgesOf(lamp.along, lamp.across);
	lamp.radiance = { 3, 3, 3 };
	lamp.emitted  = 3 * math::pi;
	return lamp;
}

Lamp sunLamp() {
	Lamp lamp      = testLamp(LightKind::Sun);
	lamp.direction = math::normalized(math::Vector { 0.3, 0.2, 1 });
	lamp.cosine    = std::cos(0.05);
	lamp.radiance  = { 5, 5, 5 };
	return lamp;
}

struct LampSurvey {
	double aimed;
	double even;
	double densityGap;
	double meetGap;
};

LampSurvey surveyedLamp(const Lights& lights) {
	std::mt19937 random(3);
	std::uniform_real_distribution<double> unit(0, 1);
	const Wavelengths wavelengths = sampledWavelengths(0.25);
	const math::Vector normal     = { 0, 0, 1 };
	const math::Vector from       = { 0.1, 0.05, 0 };
	const int count               = 200000;
	LampSurvey survey             = { 0, 0, 0, 0 };
	for (int i = 0; i < count; ++i) {
		const math::UnitPair pair = { unit(random), unit(random) };

		const AimQuery query = {
			.light       = 0,
			.from        = from,
			.normal      = normal,
			.pair        = pair,
			.wavelengths = wavelengths,
		};

		const LightAim aim = aimLight(lights, query);
		if (aim.found && aim.density > 0) {
			survey.aimed += laneMean(aim.radiance) * std::max(math::dot(normal, aim.toward), 0.0) / count;

			const AimDensityQuery asked = {
				.light    = 0,
				.from     = from,
				.toward   = aim.toward,
				.distance = aim.distance,
			};

			const MeetQuery meet = {
				.light       = 0,
				.origin      = from,
				.direction   = aim.toward,
				.limit       = std::numeric_limits<double>::infinity(),
				.wavelengths = wavelengths,
			};

			const LightMeet met = meetLight(lights, meet);
			survey.densityGap   = std::max(survey.densityGap, std::abs(aimDensity(lights, asked) / aim.density - 1));
			survey.meetGap      = std::max(survey.meetGap, met.met ? std::abs(laneMean(met.radiance) / (laneMean(aim.radiance) * aim.density) - 1) : 1.0);
		}
		const math::Vector even = math::cosineHemisphere(math::basisAround(normal), pair);

		const MeetQuery ray = {
			.light       = 0,
			.origin      = from,
			.direction   = even,
			.limit       = std::numeric_limits<double>::infinity(),
			.wavelengths = wavelengths,
		};

		const LightMeet met = meetLight(lights, ray);
		survey.even += met.met ? laneMean(met.radiance) * math::pi / count : 0;
	}
	return survey;
}

void checkLampCase(const LampCase& lampCase) {
	const std::string name = lampCase.name;
	const GradientSky sky  = {
		.up      = { 0, 0, 1 },
		.zenith  = { 0, 0, 0 },
		.horizon = { 0, 0, 0 },
		.ground  = { 0, 0, 0 },
	};

	const Lights lights = {
		.lamps = { lampCase.lamp },
		.sky   = sky,
	};

	const LampSurvey survey = surveyedLamp(lights);
	std::printf("light %s: irradiance by aim %.4f, by cosine draws %.4f, density gap %.1e, meet gap %.1e\n", lampCase.name, survey.aimed, survey.even, survey.densityGap, survey.meetGap);
	checkNear(Near { survey.aimed / survey.even, 1, lampCase.tolerance, (name + ": aiming and meeting give the same irradiance").c_str() });
	check(survey.densityGap < 1e-6, (name + ": aimDensity is the density an aim reports").c_str());
	check(survey.meetGap < 1e-6, (name + ": a ray along an aim meets the light with its radiance").c_str());
}

void checkSkyAndShares() {
	const GradientSky sky = {
		.up      = { 0, 0, 1 },
		.zenith  = { 0.2, 0.2, 0.2 },
		.horizon = { 0.6, 0.6, 0.6 },
		.ground  = { 0.1, 0.1, 0.1 },
	};

	const Wavelengths wavelengths = sampledWavelengths(0.5);
	const math::Vector up         = { 0, 0, 1 };
	const math::Vector level      = { 1, 0, 0 };
	checkNear(Near { laneMean(skyRadiance(sky, up, wavelengths)), 0.2, 1e-12, "the sky straight up is the zenith" });
	checkNear(Near { laneMean(skyRadiance(sky, level, wavelengths)), 0.6, 1e-12, "the sky looking level is the horizon" });

	const Lights lights = {
		.lamps = { sphereLamp(), quadLamp(true), sunLamp() },
		.sky   = sky,
	};

	const std::vector<double> shares = lampShares(lights);
	checkNear(Near { shares[0] + shares[1] + shares[2], 1, 1e-12, "lamp shares sum to one" });
	check(shares[2] == 0, "a sun starts no paths");
}

void checkSpectrumTable() {
	SpectrumTable table = {
		.values = {},
	};

	for (int k = 0; k < tablePlaces; ++k) {
		table.values[k] = std::sin(0.3 * k) + 2;
	}
	double worst = 0;
	for (int k = 0; k < tablePlaces; ++k) {
		const double nanometres[4] = { shortestWavelength + (longestWavelength - shortestWavelength) * k / (tablePlaces - 1), 500, 600, 650 };
		const double density[4]    = { 1, 1, 1, 1 };
		worst                      = std::max(worst, std::abs(spectrumTableAt(table, wavelengthsAt(nanometres, density)).values[0] - table.values[k]));
	}
	check(worst < 1e-12, "a spectrum table reads its own values at its places");
}

double meanThrough(const Cloudscape& scape, bool exact) {
	std::mt19937 random(5);
	std::uniform_real_distribution<double> unit(0, 1);
	double sum = 0;
	for (int k = 0; k < 1000; ++k) {
		const math::UnitPair pair    = { unit(random), unit(random) };
		const math::Vector direction = math::uniformSphere(pair);
		const math::Vector up        = { direction.x, direction.y, std::abs(direction.z) * 0.95 + 0.05 };
		sum += veiled(scape, math::normalized(up), exact).through;
	}
	return sum / 1000;
}

void checkClouds() {
	const SceneFile file = sceneOrExit(writeScene("clouds", R"(
		function ball(r) {
			return length(p) - r;
		}

		sky physical {
			haze = 1.3;
		}

		clouds {
			height    = 1500;
			thickness = 800;
			cover     = 0.5;
			density   = 0.03;
			span      = 2500;
		}

		light sun {
			direction = [-0.5, 0.3, 0.6];
			power     = [3, 3, 3];
		}

		ball(1);
	)"));
	const Features one   = featuresOf(featureQueryOf(file, 1));
	const Features eight = featuresOf(featureQueryOf(file, 8));
	bool same            = one.clouds.through == eight.clouds.through;
	for (std::size_t k = 0; same && k < one.clouds.shown.size(); ++k) {
		const Colour& a = one.clouds.shown[k];
		const Colour& b = eight.clouds.shown[k];
		same            = a.red == b.red && a.green == b.green && a.blue == b.blue;
	}
	const double exact = meanThrough(one.clouds, true);
	const double table = meanThrough(one.clouds, false);
	std::printf("light clouds: mean share let through over 1000 directions above the horizon %.4f marched, %.4f off the table\n", exact, table);
	check(same, "the cloud table is the same on 1 and 8 threads");
	check(std::abs(exact - table) < 0.02 && exact < 0.98, "the clouds' table agrees with their full march within 2%");
}

void checkLight() {
	const LampCase cases[] = {
		{ "sphere", sphereLamp(), 0.02 },
		{ "square quad", quadLamp(true), 0.02 },
		{ "skew quad", quadLamp(false), 0.02 },
		{ "sun", sunLamp(), 0.1 },
	};

	for (const LampCase& lampCase : cases) {
		checkLampCase(lampCase);
	}
	checkSkyAndShares();
	checkSpectrumTable();
	checkClouds();
}

}
