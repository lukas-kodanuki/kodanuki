#include "syntax/compile.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace kodanuki {

bool isRuntime(const Value& value) {
	if (value.kind == ValueKind::Number) {
		return true;
	}
	if (value.kind != ValueKind::List) {
		return false;
	}
	return std::all_of(value.items.begin(), value.items.end(), [](const Value& item) {
		return item.kind == ValueKind::Number;
	});
}

void describe(std::ostringstream& out, const Value& value) {
	if (value.kind == ValueKind::List) {
		out << '[';
		for (const Value& item : value.items) {
			describe(out, item);
			out << ',';
		}
		out << ']';
		return;
	}
	if (value.kind == ValueKind::Word) {
		out << value.word;
		return;
	}
	out << value.number << '/' << value.material;
}

void describeArgument(std::ostringstream& out, const Value& argument) {
	if (isRuntime(argument)) {
		out << '#' << (argument.kind == ValueKind::List ? argument.items.size() : 0);
		return;
	}
	describe(out, argument);
}

std::string specialisationKey(const Pattern& pattern) {
	std::ostringstream out;
	out.precision(17);
	out << pattern.function->text << '(';
	for (const Value& argument : pattern.arguments) {
		describeArgument(out, argument);
		out << ',';
	}
	out << ')';
	return out.str();
}

Value argumentValue(Compiler& compiler, const Value& argument) {
	if (!isRuntime(argument)) {
		return argument;
	}
	if (argument.kind == ValueKind::Number) {
		const std::uint32_t index = compiler.program.arguments++;

		const Step argument = {
			.opcode = Opcode::Argument,
			.a      = index,
			.b      = 0,
			.c      = 0,
			.value  = 0,
			.leaf   = noLeaf,
		};

		return slotValue(pushStep(compiler, argument));
	}
	std::vector<Value> items;
	for (const Value& item : argument.items) {
		items.push_back(argumentValue(compiler, item));
	}
	return listValue(items);
}

std::vector<Value> argumentValues(Compiler& compiler, const std::vector<Value>& arguments) {
	std::vector<Value> values;
	for (const Value& argument : arguments) {
		values.push_back(argumentValue(compiler, argument));
	}
	return values;
}

Value inputVector(Compiler& compiler, Opcode x) {
	std::vector<Value> items;
	for (int i = 0; i < 3; ++i) {
		const Step input = {
			.opcode = Opcode(int(x) + i),
			.a      = 0,
			.b      = 0,
			.c      = 0,
			.value  = 0,
			.leaf   = noLeaf,
		};

		items.push_back(slotValue(pushStep(compiler, input)));
	}
	return listValue(items);
}

void startProgram(Compiler& compiler, const Pattern& pattern) {
	compiler.program.name = pattern.function->text;
	compiler.point        = inputVector(compiler, Opcode::PointX);
	compiler.normal       = inputVector(compiler, Opcode::NormalX);
}

Compiler emptyCompiler() {
	const Program program = {
		.name      = {},
		.steps     = {},
		.result    = 0,
		.arguments = 0,
	};

	return Compiler {
		.program   = program,
		.constants = {},
		.point     = numberValue(0),
		.normal    = numberValue(0),
		.returned  = false,
		.result    = numberValue(0),
	};
}

Program compileFunction(Evaluator& evaluator, const Pattern& pattern) {
	Compiler compiler                  = emptyCompiler();
	Compiler* const saved              = evaluator.compiler;
	evaluator.compiler                 = &compiler;
	const std::vector<Value> arguments = argumentValues(compiler, pattern.arguments);
	startProgram(compiler, pattern);

	const Invocation invocation = {
		.definition = *pattern.function,
		.arguments  = arguments,
		.context    = rootContext(),
		.location   = pattern.location,
	};

	const Value result = inlineFunction(evaluator, invocation);
	if (result.kind != ValueKind::Number) {
		fail(pattern.function->location, pattern.function->text + " must return a number");
	}
	compiler.program.result = slotOf(evaluator, result);
	evaluator.compiler      = saved;
	return compiler.program;
}

std::uint32_t specialise(Evaluator& evaluator, const Pattern& pattern) {
	if (failed()) {
		return 0;
	}
	const std::string key = specialisationKey(pattern);
	const auto found      = evaluator.specialised.find(key);
	if (found != evaluator.specialised.end()) {
		return found->second;
	}
	evaluator.scene.programs.push_back(compileFunction(evaluator, pattern));
	const std::uint32_t id     = std::uint32_t(evaluator.scene.programs.size() - 1);
	evaluator.specialised[key] = id;
	return id;
}

void pushArgument(Evaluator& evaluator, const Value& argument) {
	if (argument.kind == ValueKind::Number) {
		evaluator.scene.arguments.push_back(argument.number);
		return;
	}
	for (const Value& item : argument.items) {
		pushArgument(evaluator, item);
	}
}

void pushArguments(Evaluator& evaluator, const std::vector<Value>& arguments) {
	for (const Value& argument : arguments) {
		if (isRuntime(argument)) {
			pushArgument(evaluator, argument);
		}
	}
}

}
