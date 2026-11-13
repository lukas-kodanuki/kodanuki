#include "plugins/feature/path_rules/path_rules.h"

#include "core/plugin.h"

namespace kodanuki {

const bool pathRulesRead = pluginBuilt("feature/path_rules");

constexpr std::uint8_t runBits    = std::uint8_t(RuleBit::Diffused) | std::uint8_t(RuleBit::Refused);
constexpr std::uint8_t vertexBits = runBits | std::uint8_t(RuleBit::Sharp);

PathRules pathRulesOf(const Scene& scene, const Lighting& lighting) {
	PathRules rules = {
		.rules    = {},
		.groups   = {},
		.reaches  = {},
		.caustics = {},
	};

	for (const Leaf& leaf : scene.leaves) {
		rules.rules.push_back(leaf.rules);
		rules.groups.push_back(leaf.groups);
	}
	for (const Light& light : lighting.lights) {
		rules.reaches.push_back(light.reaches);
		rules.caustics.push_back(light.caustics);
	}
	return rules;
}

bool sceneHasRules(const Scene& scene, const Lighting& lighting) {
	if (!pathRulesRead) {
		return false;
	}
	for (const Leaf& leaf : scene.leaves) {
		if (leaf.rules != 0) {
			return true;
		}
	}
	for (const Light& light : lighting.lights) {
		if (light.reaches != 0 || !light.caustics) {
			return true;
		}
	}
	return false;
}

RuleState freshRules() {
	return RuleState {
		.bits     = 0,
		.drawn    = 0,
		.standing = noLeaf,
		.drawnOn  = noLeaf,
		.beyond   = 0,
	};
}

std::uint8_t saidBy(const PathRules& rules, std::uint32_t leaf) {
	return leaf < rules.rules.size() ? rules.rules[leaf] : 0;
}

bool says(const PathRules& rules, std::uint32_t leaf, Rule rule) {
	return (saidBy(rules, leaf) & std::uint8_t(rule)) != 0;
}

std::uint8_t refusedBy(const PathRules& rules, std::uint32_t leaf) {
	return says(rules, leaf, Rule::Causticless) ? std::uint8_t(RuleBit::Refused) : 0;
}

RuleState steppedRules(const PathRules& rules, const RuleStep& step) {
	const std::uint8_t refusing = refusedBy(rules, step.leaf);
	const std::uint8_t carried  = step.state.drawn & runBits;
	RuleState stepped           = step.state;
	stepped.bits                = step.sharp ? std::uint8_t(carried | std::uint8_t(RuleBit::Sharp) | refusing) : std::uint8_t(std::uint8_t(RuleBit::Diffused) | refusing);
	stepped.standing            = step.leaf;
	return stepped;
}

RuleState lightSteppedRules(const PathRules& rules, const RuleStep& step) {
	const RuleState& state = step.state;
	const bool first       = (state.bits & (std::uint8_t(RuleBit::Diffused) | std::uint8_t(RuleBit::Sharp))) == 0;
	if ((state.bits & std::uint8_t(RuleBit::Diffused)) != 0) {
		return state;
	}
	const std::uint8_t sharp = first && step.sharp ? std::uint8_t(RuleBit::Sharp) : 0;
	const std::uint8_t ended = step.sharp ? 0 : std::uint8_t(RuleBit::Diffused);
	RuleState stepped        = state;
	stepped.bits             = std::uint8_t(state.bits | sharp | ended | refusedBy(rules, step.leaf));
	stepped.standing         = first ? step.leaf : state.standing;
	return stepped;
}

std::uint8_t connectedBits(std::uint8_t eye, const RuleState& light) {
	const std::uint8_t own = light.bits & vertexBits;
	if ((own & std::uint8_t(RuleBit::Diffused)) != 0) {
		return own;
	}
	return std::uint8_t((eye & runBits) | own);
}

bool causticRefused(std::uint8_t bits, bool refusing) {
	const std::uint8_t caustic = std::uint8_t(RuleBit::Diffused) | std::uint8_t(RuleBit::Sharp);
	return (bits & caustic) == caustic && ((bits & std::uint8_t(RuleBit::Refused)) != 0 || refusing);
}

bool reachedBy(const PathRules& rules, const RuleEnding& ending) {
	const std::uint64_t reach = rules.reaches[ending.light];
	if (reach == 0 || ending.standing == noLeaf) {
		return true;
	}
	return ending.standing < rules.groups.size() && (rules.groups[ending.standing] & reach) != 0;
}

bool rulesLetEnd(const PathRules& rules, const RuleEnding& ending) {
	if (ending.light >= rules.reaches.size()) {
		return !causticRefused(ending.bits, false);
	}
	if (causticRefused(ending.bits, !rules.caustics[ending.light])) {
		return false;
	}
	return reachedBy(rules, ending);
}

bool rulesLetGlow(const PathRules& rules, const GlowEnding& ending) {
	return !causticRefused(ending.bits, says(rules, ending.leaf, Rule::Causticless));
}

bool rulesPass(const PathRules& rules, std::uint32_t leaf, Passage passage) {
	switch (passage) {
		case Passage::ToLens: return says(rules, leaf, Rule::Hidden);
		case Passage::ToLight: return says(rules, leaf, Rule::Shadowless);
		case Passage::Between: return false;
	}

	return false;
}

}
