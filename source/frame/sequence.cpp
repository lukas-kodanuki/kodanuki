#include "frame/sequence.h"

#include <chrono>
#include <utility>

namespace kodanuki {

Change joinedChange(Change a, Change b) {
	return Change(std::uint8_t(a) | std::uint8_t(b));
}

bool changedBy(Change change, Change flag) {
	return (std::uint8_t(change) & std::uint8_t(flag)) != 0;
}

bool sameVector(const math::Vector& a, const math::Vector& b) {
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool sameSize(const Lens& a, const Lens& b) {
	return a.width == b.width && a.height == b.height;
}

bool sameLens(const Lens& a, const Lens& b) {
	const bool placed = sameVector(a.eye, b.eye) && sameVector(a.forward, b.forward) && sameVector(a.right, b.right) && sameVector(a.up, b.up);
	const bool shaped = a.reach == b.reach && a.aspect == b.aspect && a.aperture == b.aperture && a.focus == b.focus;
	return placed && shaped && sameSize(a, b);
}

Change lensChange(const FrameStep& last, const StepQuery& query) {
	const Lens shownBefore = shownLens(last.lens, last.film);
	const Lens shownNext   = shownLens(query.lens, query.film);
	const Change moved     = sameLens(last.lens, query.lens) ? Change::None : Change::Moved;
	return joinedChange(moved, sameSize(shownBefore, shownNext) ? Change::None : Change::Cut);
}

FrameStep firstStep(const StepQuery& query) {
	return FrameStep {
		.index       = 0,
		.lens        = query.lens,
		.before      = query.lens,
		.film        = query.film,
		.change      = joinedChange(query.change, Change::Cut),
		.still       = 0,
		.keepsGuides = query.keepsGuides,
	};
}

FrameStep nextStep(const FrameStep& last, const StepQuery& query) {
	const Change change = joinedChange(query.change, lensChange(last, query));
	return FrameStep {
		.index       = last.index + 1,
		.lens        = query.lens,
		.before      = last.lens,
		.film        = query.film,
		.change      = change,
		.still       = change == Change::None ? last.still + 1 : 0,
		.keepsGuides = query.keepsGuides && sameSize(last.lens, query.lens),
	};
}

struct SequencePrepared {
	Prepared kept;
	Prepared all;
};

bool keepsLoad(const FrameStep& step) {
	const Change restarts = joinedChange(Change::Cut, joinedChange(Change::Lights, Change::Scene));
	return !changedBy(step.change, restarts);
}

Prepared keptPreparations(const SequenceInput& input, const FrameBasis& basis, const History& history) {
	if (keepsLoad(input.step)) {
		return history.kept;
	}

	const PreparationRun run = {
		.start = input.frame.chosen,
		.reads = PreparationReads::Load,
	};

	return preparedFrom(input.frame, basis, run);
}

std::vector<PreparationMember> unguidedPreparations(const std::vector<PreparationMember>& members) {
	std::vector<PreparationMember> unguided;
	for (const PreparationMember member : members) {
		if (member != PreparationMember::GuideSurvey) {
			unguided.push_back(member);
		}
	}
	return unguided;
}

Prepared guidedPreparations(const SequenceInput& input, const FrameBasis& basis, const History& history) {
	const FrameInput& frame                           = input.frame;
	const std::vector<PreparationMember> preparations = unguidedPreparations(frame.preparations);

	const FrameInput unguided = {
		.geometry     = frame.geometry,
		.looks        = frame.looks,
		.lighting     = frame.lighting,
		.shot         = frame.shot,
		.lens         = frame.lens,
		.chosen       = frame.chosen,
		.preparations = preparations,
		.features     = frame.features,
		.stopping     = frame.stopping,
		.smoothGlass  = frame.smoothGlass,
		.execution    = frame.execution,
		.threads      = frame.threads,
		.progress     = frame.progress,
	};

	const PreparationRun run = {
		.start = history.prepared,
		.reads = PreparationReads::Camera,
	};

	return preparedFrom(unguided, basis, run);
}

bool keepsGuidesOf(const FrameStep& step) {
	return step.keepsGuides && step.change == Change::Moved;
}

SequencePrepared sequencePrepared(const SequenceInput& input, const FrameBasis& basis, const History& history) {
	const Prepared kept = keptPreparations(input, basis, history);
	if (input.step.change == Change::None) {
		return SequencePrepared {
			.kept = kept,
			.all  = history.prepared,
		};
	}
	if (keepsGuidesOf(input.step)) {
		return SequencePrepared {
			.kept = kept,
			.all  = guidedPreparations(input, basis, history),
		};
	}

	const PreparationRun run = {
		.start = kept,
		.reads = PreparationReads::Camera,
	};

	return SequencePrepared {
		.kept = kept,
		.all  = preparedFrom(input.frame, basis, run),
	};
}

Tracing sequenceTracing(const SequenceInput& input, const FrameBasis& basis, const Prepared& prepared) {
	Tracing tracing   = frameTracing(input.frame, basis, prepared);
	tracing.film      = input.step.film;
	tracing.recording = true;
	return tracing;
}

FrameRecord frameRecordOf(const Lens& lens, const std::vector<Recorded>& records) {
	FrameRecord record = {
		.width    = lens.width,
		.height   = lens.height,
		.parts    = {},
		.surfaces = {},
	};

	record.parts.reserve(records.size());
	record.surfaces.reserve(records.size());
	for (const Recorded& recorded : records) {
		record.parts.push_back(recorded.parts);
		record.surfaces.push_back(recorded.surface);
	}
	return record;
}

bool continuesEstimates(const SequenceInput& input, const History& history) {
	return input.step.change == Change::None && history.estimates.size() == std::size_t(input.frame.lens.width) * std::size_t(input.frame.lens.height);
}

History emptyHistory() {
	const FrameRecord none = {
		.width    = 0,
		.height   = 0,
		.parts    = {},
		.surfaces = {},
	};

	return History {
		.record         = none,
		.estimates      = {},
		.kept           = corePrepared(),
		.prepared       = corePrepared(),
		.reconstruction = emptyReconstruction(),
	};
}

double stepSecondsSince(std::chrono::steady_clock::time_point start) {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::vector<Surface> shownSurfaces(const SequenceInput& input, const Tracing& tracing) {
	if (!input.surfaced) {
		return {};
	}

	const SurfaceQuery query = {
		.member  = input.frame.execution,
		.tracing = tracing,
		.lens    = input.frame.lens,
		.film    = input.step.film,
		.threads = input.frame.threads,
	};

	return surfacesOf(query);
}

Stepped steppedFrame(const SequenceInput& input, History history) {
	const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
	const FrameBasis basis                              = frameBasis(input.frame);
	SequencePrepared prepared                           = sequencePrepared(input, basis, history);
	const Tracing tracing                               = sequenceTracing(input, basis, prepared.all);
	const std::vector<Estimate> none                    = {};
	const double preparations                           = stepSecondsSince(started);

	const SteppedRun run = {
		.tracing   = tracing,
		.stopping  = basis.stopping,
		.prepared  = prepared.all,
		.estimates = continuesEstimates(input, history) ? history.estimates : none,
		.index     = input.step.index,
		.execution = input.frame.execution,
		.threads   = input.frame.threads,
		.progress  = input.frame.progress,
	};

	const std::chrono::steady_clock::time_point traced = std::chrono::steady_clock::now();
	SteppedPass pass                                   = steppedPass(run);
	const double trace                                 = stepSecondsSince(traced);

	FrameRecord record     = frameRecordOf(input.frame.lens, pass.records);
	const Lens shown       = shownLens(input.frame.lens, input.step.film);
	const Lens shownBefore = shownLens(input.step.before, input.step.film);
	const Change restarts  = joinedChange(Change::Cut, joinedChange(Change::Lights, Change::Scene));

	const std::chrono::steady_clock::time_point surfacedAt = std::chrono::steady_clock::now();
	const std::vector<Surface> surfaces                    = shownSurfaces(input, tracing);
	const double surfacing                                 = stepSecondsSince(surfacedAt);

	const ShownFrame frame = {
		.width     = shown.width,
		.height    = shown.height,
		.lens      = shown,
		.before    = shownBefore,
		.film      = input.step.film,
		.moved     = changedBy(input.step.change, Change::Moved),
		.restarted = changedBy(input.step.change, restarts),
		.still     = input.step.still,
		.record    = record,
		.estimates = pass.estimates,
		.surfaces  = surfaces,
		.threads   = input.frame.threads,
	};

	const ReconstructionQuery query = {
		.frame    = frame,
		.prepared = prepared.all,
	};

	const std::chrono::steady_clock::time_point shownAt = std::chrono::steady_clock::now();
	Reconstructed reconstructed                         = reconstructedFrame(input.reconstruction, query, std::move(history.reconstruction));
	const double reconstruction                         = stepSecondsSince(shownAt);
	const std::chrono::steady_clock::time_point kept    = std::chrono::steady_clock::now();

	History next = {
		.record         = std::move(record),
		.estimates      = std::move(pass.estimates),
		.kept           = std::move(prepared.kept),
		.prepared       = std::move(prepared.all),
		.reconstruction = std::move(reconstructed.history),
	};

	const StepTimes times = {
		.preparations   = preparations,
		.trace          = trace,
		.surfaces       = surfacing,
		.reconstruction = reconstruction,
		.keeping        = stepSecondsSince(kept),
	};

	return Stepped {
		.image   = std::move(reconstructed.image),
		.history = std::move(next),
		.times   = times,
	};
}

}
