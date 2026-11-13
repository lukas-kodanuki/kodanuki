#include "syntax/evaluator.h"

#include "sdf/numbers.h"
#include "syntax/compile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kodanuki {

Value numberValue(double number) {
	return Value {
		.kind     = ValueKind::Number,
		.number   = number,
		.slot     = -1,
		.items    = {},
		.material = 0,
		.pattern  = nullptr,
		.word     = {},
	};
}

Value listValue(std::vector<Value> items) {
	return Value {
		.kind     = ValueKind::List,
		.number   = 0,
		.slot     = -1,
		.items    = std::move(items),
		.material = 0,
		.pattern  = nullptr,
		.word     = {},
	};
}

Value slotValue(std::uint32_t slot) {
	return Value {
		.kind     = ValueKind::Number,
		.number   = 0,
		.slot     = std::int32_t(slot),
		.items    = {},
		.material = 0,
		.pattern  = nullptr,
		.word     = {},
	};
}

Value patternValue(const Pattern& pattern) {
	return Value {
		.kind     = ValueKind::Pattern,
		.number   = 0,
		.slot     = -1,
		.items    = {},
		.material = 0,
		.pattern  = std::make_shared<const Pattern>(pattern),
		.word     = {},
	};
}

bool isKnown(const Value& value) {
	return value.kind == ValueKind::Number && value.slot < 0;
}

bool sameItems(const Value& a, const Value& b) {
	if (a.items.size() != b.items.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.items.size(); ++i) {
		const bool same = a.items[i].slot == b.items[i].slot && a.items[i].number == b.items[i].number;
		if (!same || !sameItems(a.items[i], b.items[i])) {
			return false;
		}
	}
	return true;
}

bool sameValue(const Value& a, const Value& b) {
	const bool sameScalar = a.kind == b.kind && a.slot == b.slot && a.number == b.number;
	const bool sameObject = a.material == b.material && a.pattern == b.pattern && a.word == b.word;
	return sameScalar && sameObject && sameItems(a, b);
}

const char* kindName(ValueKind kind) {
	switch (kind) {
		case ValueKind::Number: return "a number";
		case ValueKind::List: return "a list";
		case ValueKind::Material: return "a material";
		case ValueKind::Pattern: return "a pattern";
		case ValueKind::Word: return "a word";
	}

	return "?";
}

struct Numbers {
	double a;
	double b;
	double c;
};

double fold(Opcode opcode, const Numbers& n) {
	switch (opcode) {
		case Opcode::Add: return n.a + n.b;
		case Opcode::Subtract: return n.a - n.b;
		case Opcode::Multiply: return n.a * n.b;
		case Opcode::Divide: return n.a / n.b;
		case Opcode::Negate: return -n.a;
		case Opcode::SquareRoot: return math::sqrt(n.a);
		case Opcode::Absolute: return math::abs(n.a);
		case Opcode::Floor: return math::floor(n.a);
		case Opcode::Sine: return math::sin(n.a);
		case Opcode::Cosine: return math::cos(n.a);
		case Opcode::Sign: return math::sign(n.a);
		case Opcode::ArcTangent: return math::atan2(n.a, n.b);
		case Opcode::Power: return math::pow(n.a, n.b);
		case Opcode::Minimum: return math::min(n.a, n.b);
		case Opcode::Maximum: return math::max(n.a, n.b);
		case Opcode::Less: return math::compare(math::Compare::Less, n.a, n.b);
		case Opcode::LessEqual: return math::compare(math::Compare::LessEqual, n.a, n.b);
		case Opcode::Equal: return math::compare(math::Compare::Equal, n.a, n.b);
		case Opcode::NotEqual: return math::compare(math::Compare::NotEqual, n.a, n.b);
		case Opcode::And: return math::compare(math::Compare::And, n.a, n.b);
		case Opcode::Or: return math::compare(math::Compare::Or, n.a, n.b);
		case Opcode::Not: return math::compare(math::Compare::Equal, n.a, 0);
		case Opcode::Select: return math::choose(n.a, n.b, n.c);
		default: return 0;
	}
}

std::uint32_t pushStep(Compiler& compiler, const Step& step) {
	compiler.program.steps.push_back(step);
	return std::uint32_t(compiler.program.steps.size() - 1);
}

std::uint32_t constantSlot(Compiler& compiler, double number) {
	const auto found = compiler.constants.find(number);
	if (found != compiler.constants.end()) {
		return found->second;
	}

	const Step constant = {
		.opcode = Opcode::Constant,
		.a      = 0,
		.b      = 0,
		.c      = 0,
		.value  = number,
		.leaf   = noLeaf,
	};

	const std::uint32_t slot = pushStep(compiler, constant);

	compiler.constants[number] = slot;
	return slot;
}

std::uint32_t slotOf(Evaluator& evaluator, const Value& value) {
	if (value.slot >= 0) {
		return std::uint32_t(value.slot);
	}
	return constantSlot(*evaluator.compiler, value.number);
}

void expectNumber(const Value& value, const Location& location) {
	if (value.kind == ValueKind::Word) {
		fail(location, "unknown name '" + value.word + "'");
	}
	if (value.kind != ValueKind::Number) {
		fail(location, std::string("expected a number, found ") + kindName(value.kind));
	}
}

Value emitOperation(Evaluator& evaluator, const Operation& operation) {
	const std::uint32_t a = slotOf(evaluator, operation.a);
	const std::uint32_t b = slotOf(evaluator, operation.b);
	const std::uint32_t c = slotOf(evaluator, operation.c);

	const Step step = {
		.opcode = operation.opcode,
		.a      = a,
		.b      = b,
		.c      = c,
		.value  = 0,
		.leaf   = noLeaf,
	};

	return slotValue(pushStep(*evaluator.compiler, step));
}

Value applyScalar(Evaluator& evaluator, const Operation& operation) {
	expectNumber(operation.a, operation.location);
	expectNumber(operation.b, operation.location);
	expectNumber(operation.c, operation.location);
	if (failed()) {
		return numberValue(0);
	}
	if (isKnown(operation.a) && isKnown(operation.b) && isKnown(operation.c)) {
		const Numbers operands = {
			.a = operation.a.number,
			.b = operation.b.number,
			.c = operation.c.number,
		};

		return numberValue(fold(operation.opcode, operands));
	}
	return emitOperation(evaluator, operation);
}

bool hasList(const Operation& operation) {
	return operation.a.kind == ValueKind::List || operation.b.kind == ValueKind::List || operation.c.kind == ValueKind::List;
}

std::size_t broadcastSize(const Operation& operation) {
	std::size_t size = 0;
	for (const Value* value : { &operation.a, &operation.b, &operation.c }) {
		if (value->kind != ValueKind::List) {
			continue;
		}
		if (size != 0 && size != value->items.size()) {
			fail(operation.location, "lists of different lengths");
			return 0;
		}
		size = value->items.size();
	}
	return size;
}

const Value& itemOrSelf(const Value& value, std::size_t index) {
	return value.kind == ValueKind::List ? value.items[index] : value;
}

Operation itemOperation(const Operation& operation, std::size_t i) {
	return Operation {
		.opcode   = operation.opcode,
		.a        = itemOrSelf(operation.a, i),
		.b        = itemOrSelf(operation.b, i),
		.c        = itemOrSelf(operation.c, i),
		.location = operation.location,
	};
}

Value apply(Evaluator& evaluator, const Operation& operation) {
	if (!hasList(operation)) {
		return applyScalar(evaluator, operation);
	}
	std::vector<Value> items;
	const std::size_t size = broadcastSize(operation);
	for (std::size_t i = 0; i < size; ++i) {
		items.push_back(apply(evaluator, itemOperation(operation, i)));
	}
	return listValue(items);
}

Value foldList(Evaluator& evaluator, const Operation& operation) {
	if (operation.a.kind != ValueKind::List || operation.a.items.empty()) {
		return operation.a;
	}
	Value total = operation.a.items[0];
	for (std::size_t i = 1; i < operation.a.items.size(); ++i) {
		const Operation combined = {
			.opcode   = operation.opcode,
			.a        = total,
			.b        = operation.a.items[i],
			.c        = numberValue(0),
			.location = operation.location,
		};

		total = apply(evaluator, combined);
	}
	return total;
}

Value dotValue(Evaluator& evaluator, const Operation& operation) {
	const Operation product = {
		.opcode   = Opcode::Multiply,
		.a        = operation.a,
		.b        = operation.b,
		.c        = numberValue(0),
		.location = operation.location,
	};

	const Value products = apply(evaluator, product);

	const Operation sum = {
		.opcode   = Opcode::Add,
		.a        = products,
		.b        = numberValue(0),
		.c        = numberValue(0),
		.location = operation.location,
	};

	return foldList(evaluator, sum);
}

Value lengthValue(Evaluator& evaluator, const Value& a, const Location& location) {
	if (a.kind != ValueKind::List) {
		const Operation magnitude = {
			.opcode   = Opcode::Absolute,
			.a        = a,
			.b        = numberValue(0),
			.c        = numberValue(0),
			.location = location,
		};

		return apply(evaluator, magnitude);
	}

	const Operation operands = {
		.opcode   = Opcode::Multiply,
		.a        = a,
		.b        = a,
		.c        = numberValue(0),
		.location = location,
	};

	const Value squared = dotValue(evaluator, operands);

	const Operation root = {
		.opcode   = Opcode::SquareRoot,
		.a        = squared,
		.b        = numberValue(0),
		.c        = numberValue(0),
		.location = location,
	};

	return apply(evaluator, root);
}

Value listItem(const Value& list, std::size_t index, const Location& location) {
	if (list.kind != ValueKind::List || index >= list.items.size()) {
		fail(location, "index " + std::to_string(index) + " is outside the list");
		return numberValue(0);
	}
	return list.items[index];
}

Value crossItem(Evaluator& evaluator, const Operation& operation, std::size_t i) {
	const std::size_t j = (i + 1) % 3;
	const std::size_t k = (i + 2) % 3;

	const Operation product = {
		.opcode   = Opcode::Multiply,
		.a        = listItem(operation.a, j, operation.location),
		.b        = listItem(operation.b, k, operation.location),
		.c        = numberValue(0),
		.location = operation.location,
	};

	const Value left = apply(evaluator, product);

	const Operation product2 = {
		.opcode   = Opcode::Multiply,
		.a        = listItem(operation.a, k, operation.location),
		.b        = listItem(operation.b, j, operation.location),
		.c        = numberValue(0),
		.location = operation.location,
	};

	const Value right = apply(evaluator, product2);

	const Operation difference = {
		.opcode   = Opcode::Subtract,
		.a        = left,
		.b        = right,
		.c        = numberValue(0),
		.location = operation.location,
	};

	return apply(evaluator, difference);
}

Value crossValue(Evaluator& evaluator, const Operation& operation) {
	return listValue({ crossItem(evaluator, operation, 0), crossItem(evaluator, operation, 1), crossItem(evaluator, operation, 2) });
}

std::size_t constantIndex(const Value& value, const Location& location) {
	if (!isKnown(value)) {
		fail(location, "an index must be known when the scene loads");
		return 0;
	}
	if (value.number < 0 || value.number != std::floor(value.number)) {
		fail(location, "an index is a whole number");
		return 0;
	}
	return std::size_t(value.number);
}

Value swizzle(const Value& value, const std::string& members, const Location& location) {
	std::vector<Value> items;
	for (const char member : members) {
		const std::size_t index = std::string("xyzw").find(member);
		if (index == std::string::npos) {
			fail(location, "unknown member '" + members + "'");
			return numberValue(0);
		}
		items.push_back(listItem(value, index, location));
	}
	return items.size() == 1 ? items[0] : listValue(items);
}

double srgbToLinear(double c) {
	return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

Value colourValue(const std::string& hex) {
	std::vector<Value> items;
	for (std::size_t i = 0; i < 3; ++i) {
		const double channel = double(std::stoi(hex.substr(2 * i, 2), nullptr, 16)) / 255.0;
		items.push_back(numberValue(srgbToLinear(channel)));
	}
	return listValue(items);
}

Value* findInScope(Scope& scope, const std::string& name) {
	const auto found = scope.find(name);
	return found == scope.end() ? nullptr : &found->second;
}

Value* findName(Evaluator& evaluator, const std::string& name) {
	for (std::size_t i = evaluator.scopes.size(); i > evaluator.base; --i) {
		if (Value* found = findInScope(evaluator.scopes[i - 1], name)) {
			return found;
		}
	}
	return findInScope(evaluator.scopes[0], name);
}

std::optional<Value> builtinName(const Evaluator& evaluator, const std::string& name) {
	if (name == "pi") {
		return numberValue(3.14159265358979323846);
	}
	if (evaluator.compiler && name == "p") {
		return evaluator.compiler->point;
	}
	if (evaluator.compiler && name == "n") {
		return evaluator.compiler->normal;
	}
	return std::nullopt;
}

Value lookup(Evaluator& evaluator, const std::string& name) {
	evaluator.timed = evaluator.timed || name == "time";
	if (const Value* found = findName(evaluator, name)) {
		return *found;
	}
	if (std::optional<Value> builtin = builtinName(evaluator, name)) {
		return *builtin;
	}
	return Value {
		.kind     = ValueKind::Word,
		.number   = 0,
		.slot     = -1,
		.items    = {},
		.material = 0,
		.pattern  = nullptr,
		.word     = name,
	};
}

void bind(Evaluator& evaluator, const std::string& name, const Value& value) {
	if (Value* found = findName(evaluator, name)) {
		*found = value;
		return;
	}
	evaluator.scopes.back()[name] = value;
}

Value evaluateExpression(Evaluator& evaluator, const Expression& expression);

Opcode binaryOpcode(const std::string& symbol) {
	static const std::unordered_map<std::string, Opcode> opcodes = {
		{ "+", Opcode::Add },
		{ "-", Opcode::Subtract },
		{ "*", Opcode::Multiply },
		{ "/", Opcode::Divide },
		{ "<", Opcode::Less },
		{ "<=", Opcode::LessEqual },
		{ "==", Opcode::Equal },
		{ "!=", Opcode::NotEqual },
		{ "&&", Opcode::And },
		{ "||", Opcode::Or },
	};

	return opcodes.at(symbol);
}

Value evaluateBinary(Evaluator& evaluator, const Expression& expression) {
	const Value a = evaluateExpression(evaluator, expression.items[0]);
	const Value b = evaluateExpression(evaluator, expression.items[1]);
	if (expression.text == "..") {
		fail(expression.location, "a range is only for 'for'");
		return numberValue(0);
	}
	if (expression.text == ">" || expression.text == ">=") {
		const Opcode opcode = expression.text == ">" ? Opcode::Less : Opcode::LessEqual;

		const Operation combined = {
			.opcode   = opcode,
			.a        = b,
			.b        = a,
			.c        = numberValue(0),
			.location = expression.location,
		};

		return apply(evaluator, combined);
	}

	const Operation combined2 = {
		.opcode   = binaryOpcode(expression.text),
		.a        = a,
		.b        = b,
		.c        = numberValue(0),
		.location = expression.location,
	};

	return apply(evaluator, combined2);
}

Value evaluateUnary(Evaluator& evaluator, const Expression& expression) {
	const Value a = evaluateExpression(evaluator, expression.items[0]);

	const Operation combined = {
		.opcode   = expression.text == "-" ? Opcode::Negate : Opcode::Not,
		.a        = a,
		.b        = numberValue(0),
		.c        = numberValue(0),
		.location = expression.location,
	};

	return apply(evaluator, combined);
}

Value evaluateList(Evaluator& evaluator, const Expression& expression) {
	std::vector<Value> items;
	for (const Expression& item : expression.items) {
		items.push_back(evaluateExpression(evaluator, item));
	}
	return listValue(items);
}

Value evaluateIndex(Evaluator& evaluator, const Expression& expression) {
	const Value list  = evaluateExpression(evaluator, expression.items[0]);
	const Value index = evaluateExpression(evaluator, expression.items[1]);
	return listItem(list, constantIndex(index, expression.items[1].location), expression.location);
}

Value evaluateCall(Evaluator& evaluator, const Expression& expression);

Value evaluateExpression(Evaluator& evaluator, const Expression& expression) {
	if (failed()) {
		return numberValue(0);
	}
	switch (expression.kind) {
		case ExpressionKind::Number: return numberValue(expression.number);
		case ExpressionKind::Colour: return colourValue(expression.text);
		case ExpressionKind::Name: return lookup(evaluator, expression.text);
		case ExpressionKind::List: return evaluateList(evaluator, expression);
		case ExpressionKind::Call: return evaluateCall(evaluator, expression);
		case ExpressionKind::Index: return evaluateIndex(evaluator, expression);
		case ExpressionKind::Member: return swizzle(evaluateExpression(evaluator, expression.items[0]), expression.text, expression.location);
		case ExpressionKind::Unary: return evaluateUnary(evaluator, expression);
		case ExpressionKind::Binary: return evaluateBinary(evaluator, expression);
	}

	return numberValue(0);
}

struct Arguments {
	std::vector<Value> positional;
	std::map<std::string, Value> named;
};

struct Call {
	const Arguments& arguments;
	const Expression& expression;
};

Arguments evaluateArguments(Evaluator& evaluator, const Expression& call) {
	Arguments arguments = {
		.positional = {},
		.named      = {},
	};

	for (std::size_t i = 0; i < call.items.size(); ++i) {
		const Value value = evaluateExpression(evaluator, call.items[i]);
		if (call.names[i].empty()) {
			arguments.positional.push_back(value);
			continue;
		}
		arguments.named[call.names[i]] = value;
	}
	return arguments;
}

Value positional(const Call& call, std::size_t index) {
	if (index >= call.arguments.positional.size()) {
		fail(call.expression.location, call.expression.text + " needs " + std::to_string(index + 1) + " arguments");
		return numberValue(0);
	}
	return call.arguments.positional[index];
}

void expectCount(const Call& call, std::size_t count) {
	if (call.arguments.positional.size() != count || !call.arguments.named.empty()) {
		fail(call.expression.location, call.expression.text + " takes " + std::to_string(count) + " arguments");
	}
}

Operation operationOf(const Call& call, Opcode opcode, std::size_t count) {
	expectCount(call, count);
	const Value zero = numberValue(0);
	if (failed()) {
		return Operation {
			.opcode   = opcode,
			.a        = zero,
			.b        = zero,
			.c        = zero,
			.location = call.expression.location,
		};
	}
	return Operation {
		.opcode   = opcode,
		.a        = call.arguments.positional[0],
		.b        = count > 1 ? call.arguments.positional[1] : zero,
		.c        = count > 2 ? call.arguments.positional[2] : zero,
		.location = call.expression.location,
	};
}

Value selectValue(Evaluator& evaluator, const Operation& operation) {
	if (isKnown(operation.a)) {
		return operation.a.number != 0 ? operation.b : operation.c;
	}

	const Operation selection = {
		.opcode   = Opcode::Select,
		.a        = operation.a,
		.b        = operation.b,
		.c        = operation.c,
		.location = operation.location,
	};

	return apply(evaluator, selection);
}

Value clampValue(Evaluator& evaluator, const Operation& operation) {
	const Operation larger = {
		.opcode   = Opcode::Maximum,
		.a        = operation.a,
		.b        = operation.b,
		.c        = numberValue(0),
		.location = operation.location,
	};

	const Value low = apply(evaluator, larger);

	const Operation smaller = {
		.opcode   = Opcode::Minimum,
		.a        = low,
		.b        = operation.c,
		.c        = numberValue(0),
		.location = operation.location,
	};

	return apply(evaluator, smaller);
}

Value mixValue(Evaluator& evaluator, const Operation& operation) {
	const Operation difference = {
		.opcode   = Opcode::Subtract,
		.a        = operation.b,
		.b        = operation.a,
		.c        = numberValue(0),
		.location = operation.location,
	};

	const Value span = apply(evaluator, difference);

	const Operation product = {
		.opcode   = Opcode::Multiply,
		.a        = span,
		.b        = operation.c,
		.c        = numberValue(0),
		.location = operation.location,
	};

	const Value step = apply(evaluator, product);

	const Operation sum = {
		.opcode   = Opcode::Add,
		.a        = operation.a,
		.b        = step,
		.c        = numberValue(0),
		.location = operation.location,
	};

	return apply(evaluator, sum);
}

Value normalizeValue(Evaluator& evaluator, const Operation& operation) {
	const Value length = lengthValue(evaluator, operation.a, operation.location);

	const Operation quotient = {
		.opcode   = Opcode::Divide,
		.a        = operation.a,
		.b        = length,
		.c        = numberValue(0),
		.location = operation.location,
	};

	return apply(evaluator, quotient);
}

Value countValue(const Operation& operation) {
	if (operation.a.kind != ValueKind::List) {
		fail(operation.location, "count takes a list");
		return numberValue(0);
	}
	return numberValue(double(operation.a.items.size()));
}

std::optional<Opcode> elementwiseOpcode(const std::string& name) {
	static const std::unordered_map<std::string, Opcode> opcodes = {
		{ "sqrt", Opcode::SquareRoot },
		{ "abs", Opcode::Absolute },
		{ "floor", Opcode::Floor },
		{ "sin", Opcode::Sine },
		{ "cos", Opcode::Cosine },
		{ "sign", Opcode::Sign },
		{ "atan2", Opcode::ArcTangent },
		{ "pow", Opcode::Power },
		{ "min", Opcode::Minimum },
		{ "max", Opcode::Maximum },
	};

	const auto found = opcodes.find(name);
	return found == opcodes.end() ? std::nullopt : std::optional<Opcode>(found->second);
}

std::size_t elementwiseCount(Opcode opcode) {
	return opcode == Opcode::ArcTangent || opcode == Opcode::Power || opcode == Opcode::Minimum || opcode == Opcode::Maximum ? 2 : 1;
}

double knownOf(const Value& value, const Location& location) {
	expectNumber(value, location);
	if (value.slot >= 0) {
		fail(location, "expected a number known when the scene loads");
	}
	return value.number;
}

struct TimedStop {
	double time;
	std::vector<double> values;
};

bool shapedStop(const Value& stop, const std::vector<TimedStop>& read) {
	const bool listed = stop.kind == ValueKind::List && stop.items.size() >= 2;
	return listed && (read.empty() || stop.items.size() == read[0].values.size() + 1);
}

TimedStop timedStop(const Value& stop, const Location& location) {
	std::vector<double> numbers;
	for (const Value& item : stop.items) {
		numbers.push_back(knownOf(item, location));
	}
	return TimedStop {
		.time   = numbers[0],
		.values = std::vector<double>(numbers.begin() + 1, numbers.end()),
	};
}

std::vector<TimedStop> timedStops(const Value& stops, const Location& location) {
	if (stops.kind != ValueKind::List || stops.items.empty()) {
		fail(location, "the stops are a list of [time, values...]");
		return {};
	}
	std::vector<TimedStop> read;
	for (const Value& stop : stops.items) {
		if (!shapedStop(stop, read)) {
			fail(location, "a stop is [time, values...], as long as the first");
			return {};
		}
		read.push_back(timedStop(stop, location));
		if (read.size() > 1 && read.back().time < read[read.size() - 2].time) {
			fail(location, "the stops' times rise");
			return {};
		}
	}
	return read;
}

std::size_t stopAtOrBefore(const std::vector<TimedStop>& stops, double t) {
	std::size_t at = 0;
	while (at + 1 < stops.size() && stops[at + 1].time <= t) {
		at += 1;
	}
	return at;
}

std::vector<double> blendedValues(const std::vector<double>& a, const std::vector<double>& b, double share) {
	std::vector<double> blended;
	for (std::size_t i = 0; i < a.size(); ++i) {
		blended.push_back(a[i] + (b[i] - a[i]) * share);
	}
	return blended;
}

bool inCurve(const std::vector<TimedStop>& stops, std::size_t from, std::size_t to) {
	return to < stops.size() && stops[from].time != stops[to].time;
}

std::vector<double> slopeBetween(const TimedStop& a, const TimedStop& b) {
	std::vector<double> slope;
	for (std::size_t i = 0; i < a.values.size(); ++i) {
		slope.push_back((b.values[i] - a.values[i]) / (b.time - a.time));
	}
	return slope;
}

std::vector<double> slopeAt(const std::vector<TimedStop>& stops, std::size_t at) {
	const bool before      = at > 0 && inCurve(stops, at, at - 1);
	const bool after       = inCurve(stops, at, at + 1);
	const TimedStop& first = before ? stops[at - 1] : stops[at];
	const TimedStop& last  = after ? stops[at + 1] : stops[at];
	if (!before && !after) {
		return std::vector<double>(stops[at].values.size(), 0.0);
	}
	return slopeBetween(first, last);
}

std::vector<double> hermiteValues(const std::vector<TimedStop>& stops, std::size_t at, double share) {
	const double s                 = share;
	const double span              = stops[at + 1].time - stops[at].time;
	const double weights[4]        = { 2 * s * s * s - 3 * s * s + 1, (s * s * s - 2 * s * s + s) * span, -2 * s * s * s + 3 * s * s, (s * s * s - s * s) * span };
	const std::vector<double> from = slopeAt(stops, at);
	const std::vector<double> to   = slopeAt(stops, at + 1);
	std::vector<double> values;
	for (std::size_t i = 0; i < from.size(); ++i) {
		values.push_back(weights[0] * stops[at].values[i] + weights[1] * from[i] + weights[2] * stops[at + 1].values[i] + weights[3] * to[i]);
	}
	return values;
}

std::vector<double> stopsAt(const std::vector<TimedStop>& stops, double t, bool smooth) {
	const std::size_t at = stopAtOrBefore(stops, t);
	if (t <= stops[at].time || at + 1 == stops.size()) {
		return stops[at].values;
	}
	const double share = (t - stops[at].time) / (stops[at + 1].time - stops[at].time);
	return smooth ? hermiteValues(stops, at, share) : blendedValues(stops[at].values, stops[at + 1].values, share);
}

Value numbersValue(const std::vector<double>& numbers) {
	if (numbers.size() == 1) {
		return numberValue(numbers[0]);
	}
	std::vector<Value> items;
	for (const double number : numbers) {
		items.push_back(numberValue(number));
	}
	return listValue(items);
}

Value stopsValue(const Call& call, bool smooth) {
	expectCount(call, 2);
	if (failed()) {
		return numberValue(0);
	}
	const std::vector<TimedStop> stops = timedStops(call.arguments.positional[0], call.expression.location);
	const double t                     = knownOf(call.arguments.positional[1], call.expression.location);
	if (failed()) {
		return numberValue(0);
	}
	return numbersValue(stopsAt(stops, t, smooth));
}

std::optional<Value> evaluateMath(Evaluator& evaluator, const Call& call) {
	const std::string& name = call.expression.text;
	if (std::optional<Opcode> opcode = elementwiseOpcode(name)) {
		return apply(evaluator, operationOf(call, *opcode, elementwiseCount(*opcode)));
	}
	if (name == "clamp") {
		return clampValue(evaluator, operationOf(call, Opcode::Add, 3));
	}
	if (name == "mix") {
		return mixValue(evaluator, operationOf(call, Opcode::Add, 3));
	}
	if (name == "select") {
		return selectValue(evaluator, operationOf(call, Opcode::Select, 3));
	}
	if (name == "dot") {
		return dotValue(evaluator, operationOf(call, Opcode::Add, 2));
	}
	if (name == "cross") {
		return crossValue(evaluator, operationOf(call, Opcode::Add, 2));
	}
	if (name == "length") {
		return lengthValue(evaluator, operationOf(call, Opcode::Add, 1).a, call.expression.location);
	}
	if (name == "normalize") {
		return normalizeValue(evaluator, operationOf(call, Opcode::Add, 1));
	}
	if (name == "all" || name == "any") {
		return foldList(evaluator, operationOf(call, name == "all" ? Opcode::And : Opcode::Or, 1));
	}
	if (name == "count") {
		return countValue(operationOf(call, Opcode::Add, 1));
	}
	if (name == "linear" || name == "smooth") {
		return stopsValue(call, name == "smooth");
	}
	return std::nullopt;
}

std::vector<float> floats(const Value& value, std::size_t count, const Location& location) {
	if (isKnown(value)) {
		return std::vector<float>(count, float(value.number));
	}
	if (value.kind != ValueKind::List || value.items.size() != count) {
		fail(location, "expected " + std::to_string(count) + " numbers");
		return std::vector<float>(count, 0.0f);
	}
	std::vector<float> out;
	for (const Value& item : value.items) {
		expectNumber(item, location);
		out.push_back(float(item.number));
	}
	return out;
}

float floatOf(const Value& value, const Location& location) {
	return float(knownOf(value, location));
}

std::vector<float> rampStops(const Pattern& pattern) {
	std::vector<float> stops;
	for (const Value& stop : pattern.stops) {
		const std::vector<float> colour = floats(stop, 3, pattern.location);
		stops.insert(stops.end(), colour.begin(), colour.end());
	}
	return stops;
}

Albedo colourAlbedo(const std::vector<float>& colour) {
	return Albedo {
		.stops     = colour,
		.program   = -1,
		.arguments = 0,
		.world     = false,
	};
}

Albedo patternAlbedo(Evaluator& evaluator, const Pattern& pattern) {
	Albedo albedo = {
		.stops     = rampStops(pattern),
		.program   = std::int32_t(specialise(evaluator, pattern)),
		.arguments = std::uint32_t(evaluator.scene.arguments.size()),
		.world     = pattern.world,
	};

	pushArguments(evaluator, pattern.arguments);
	return albedo;
}

Albedo rampAlbedo(Evaluator& evaluator, const Pattern& pattern) {
	if (pattern.stops.empty()) {
		fail(pattern.location, "a pattern paints through ramp(pattern, colours...)");
		return colourAlbedo({ 0, 0, 0 });
	}
	return patternAlbedo(evaluator, pattern);
}

Albedo makeAlbedo(Evaluator& evaluator, const Value& value, const Location& location) {
	if (value.kind != ValueKind::Pattern) {
		return Albedo {
			.stops     = floats(value, 3, location),
			.program   = -1,
			.arguments = 0,
			.world     = false,
		};
	}
	return rampAlbedo(evaluator, *value.pattern);
}

struct MaterialField {
	const std::string& name;
	const Value& value;
	const Location& location;
};

void expectGlass(const Material& material, const MaterialField& field) {
	if (material.kind != MaterialKind::Glass) {
		fail(field.location, "only a glass takes '" + field.name + "'");
	}
}

void expectFilmed(const Material& material, const MaterialField& field) {
	if (material.kind == MaterialKind::Matte) {
		fail(field.location, "a matte takes no film");
	}
}

void readMaterialField(Material& material, const MaterialField& field) {
	const std::string& name = field.name;
	if (name == "roughness" || name == "refraction" || name == "strength") {
		float& into = name == "roughness" ? material.roughness : (name == "refraction" ? material.refraction : material.strength);
		into        = floatOf(field.value, field.location);
		return;
	}
	if (name == "film") {
		expectFilmed(material, field);
		const std::vector<float> film = floats(field.value, 2, field.location);
		material.film[0]              = film[0];
		material.film[1]              = film[1];
		return;
	}
	if (name == "dispersion" || name == "scatter" || name == "lean") {
		expectGlass(material, field);
		float& into = name == "dispersion" ? material.dispersion : (name == "scatter" ? material.scatter : material.lean);
		into        = floatOf(field.value, field.location);
		if (material.scatter < 0 || !(std::abs(material.lean) < 1)) {
			fail(field.location, "a scatter is 0 or more and a lean between -1 and 1");
		}
		return;
	}
	if (name == "guided" || name == "relays" || name == "aimed") {
		bool& into = name == "guided" ? material.guided : (name == "relays" ? material.relays : material.aimed);
		into       = floatOf(field.value, field.location) != 0;
		return;
	}
	if (name == "absorb" || name == "glow") {
		Colour& into = name == "absorb" ? material.absorb : material.glow;
		into         = colourFromFloats(floats(field.value, 3, field.location));
		return;
	}
	fail(field.location, "a material has no field '" + name + "'");
}

std::optional<MaterialKind> materialKind(const std::string& name) {
	static const std::unordered_map<std::string, MaterialKind> kinds = {
		{ "matte", MaterialKind::Matte },
		{ "plastic", MaterialKind::Plastic },
		{ "metal", MaterialKind::Metal },
		{ "glass", MaterialKind::Glass },
	};

	const auto found = kinds.find(name);
	return found == kinds.end() ? std::nullopt : std::optional<MaterialKind>(found->second);
}

Value pushMaterial(Evaluator& evaluator, const Material& material) {
	evaluator.looks.materials.push_back(material);
	return Value {
		.kind     = ValueKind::Material,
		.number   = 0,
		.slot     = -1,
		.items    = {},
		.material = std::uint32_t(evaluator.looks.materials.size() - 1),
		.pattern  = nullptr,
		.word     = {},
	};
}

std::optional<Value> evaluateMaterial(Evaluator& evaluator, const Call& call) {
	const std::optional<MaterialKind> kind = materialKind(call.expression.text);
	if (!kind) {
		return std::nullopt;
	}

	Material material = {
		.kind   = *kind,
		.albedo = Albedo {
			.stops     = { 1, 1, 1 },
			.program   = -1,
			.arguments = 0,
			.world     = false,
		},

		.roughness  = 0.5f,
		.refraction = 1.5f,
		.dispersion = 0,
		.scatter    = 0,
		.lean       = 0,
		.absorb     = { 0, 0, 0 },
		.glow       = { 0, 0, 0 },
		.strength   = 0,
		.film       = { 0, 1 },
		.guided     = true,
		.relays     = true,
		.aimed      = true,
	};

	if (material.kind != MaterialKind::Glass) {
		material.albedo = makeAlbedo(evaluator, positional(call, 0), call.expression.location);
	}
	for (const auto& [name, value] : call.arguments.named) {
		const MaterialField field = {
			.name     = name,
			.value    = value,
			.location = call.expression.location,
		};

		readMaterialField(material, field);
	}
	return pushMaterial(evaluator, material);
}

std::optional<Pattern> patternArgument(const Call& call) {
	const Value first = positional(call, 0);
	if (first.kind != ValueKind::Pattern) {
		fail(call.expression.location, call.expression.text + " takes a pattern first");
		return std::nullopt;
	}
	return *first.pattern;
}

Value worldPattern(const Call& call) {
	expectCount(call, 1);
	const std::optional<Pattern> pattern = patternArgument(call);
	if (!pattern) {
		return numberValue(0);
	}
	Pattern world = *pattern;
	world.world   = true;
	return patternValue(world);
}

Value rampPattern(const Call& call) {
	if (call.arguments.positional.size() < 3) {
		fail(call.expression.location, "ramp takes a pattern and two colours or more");
		return numberValue(0);
	}
	const std::optional<Pattern> pattern = patternArgument(call);
	if (!pattern) {
		return numberValue(0);
	}
	Pattern ramp = *pattern;
	ramp.stops.assign(call.arguments.positional.begin() + 1, call.arguments.positional.end());
	return patternValue(ramp);
}

std::optional<Value> evaluatePatternCall(const Call& call) {
	if (call.expression.text == "world") {
		return worldPattern(call);
	}
	if (call.expression.text == "ramp") {
		return rampPattern(call);
	}
	return std::nullopt;
}

void runBlock(Evaluator& evaluator, const std::vector<Statement>& body, const Context& context);

void bindParameters(Evaluator& evaluator, const Invocation& call) {
	const Statement& definition = call.definition;
	if (call.arguments.size() != definition.names.size()) {
		fail(call.location, definition.text + " takes " + std::to_string(definition.names.size()) + " arguments");
		return;
	}
	for (std::size_t i = 0; i < call.arguments.size(); ++i) {
		evaluator.scopes.back()[definition.names[i]] = call.arguments[i];
	}
}

void runDefinition(Evaluator& evaluator, const Invocation& call) {
	const std::size_t savedBase = evaluator.base;
	evaluator.base              = evaluator.scopes.size();
	evaluator.scopes.push_back(Scope {});
	bindParameters(evaluator, call);
	runBlock(evaluator, call.definition.body, call.context);
	evaluator.scopes.resize(evaluator.base);
	evaluator.base = savedBase;
}

Value inlineFunction(Evaluator& evaluator, const Invocation& call) {
	Compiler& compiler       = *evaluator.compiler;
	const bool savedReturned = compiler.returned;
	compiler.returned        = false;
	runDefinition(evaluator, call);
	if (!compiler.returned) {
		fail(call.definition.location, call.definition.text + " never returns");
	}
	compiler.returned = savedReturned;
	return compiler.result;
}

const Statement* findFunction(const Evaluator& evaluator, const Expression& call) {
	const auto found = evaluator.definitions.find(call.text);
	if (found == evaluator.definitions.end() || found->second->kind != StatementKind::Function) {
		fail(call.location, "unknown function '" + call.text + "'");
		return nullptr;
	}
	return found->second;
}

Value callFunction(Evaluator& evaluator, const Call& call) {
	const Statement* found = findFunction(evaluator, call.expression);
	if (!found) {
		return numberValue(0);
	}
	const Statement& function = *found;
	if (!call.arguments.named.empty()) {
		fail(call.expression.location, "a function takes positional arguments");
		return numberValue(0);
	}
	if (evaluator.compiler) {
		const Invocation invocation = {
			.definition = function,
			.arguments  = call.arguments.positional,
			.context    = rootContext(),
			.location   = call.expression.location,
		};

		return inlineFunction(evaluator, invocation);
	}

	const Pattern pattern = {
		.function  = &function,
		.arguments = call.arguments.positional,
		.location  = call.expression.location,
		.world     = false,
		.stops     = {},
	};

	return patternValue(pattern);
}

Value evaluateCall(Evaluator& evaluator, const Expression& expression) {
	const Arguments arguments = evaluateArguments(evaluator, expression);

	const Call call = {
		.arguments  = arguments,
		.expression = expression,
	};

	if (std::optional<Value> math = evaluateMath(evaluator, call)) {
		return *math;
	}
	if (std::optional<Value> material = evaluateMaterial(evaluator, call)) {
		return *material;
	}
	if (std::optional<Value> pattern = evaluatePatternCall(call)) {
		return *pattern;
	}
	return callFunction(evaluator, call);
}

struct Affine {
	double linear[3][3];

	double offset[3];
};

void multiply(const double a[3][3], const double b[3][3], double out[3][3]) {
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
		}
	}
}

Context composed(const Context& context, const Affine& affine) {
	Context out = context;
	multiply(context.linear, affine.linear, out.linear);
	for (int i = 0; i < 3; ++i) {
		const double* row = context.linear[i];
		out.origin[i]     = context.origin[i] + row[0] * affine.offset[0] + row[1] * affine.offset[1] + row[2] * affine.offset[2];
	}
	return out;
}

std::vector<double> vector3(const Value& value, const Location& location) {
	floats(value, 3, location);
	if (failed()) {
		return { 0, 0, 0 };
	}
	if (value.kind == ValueKind::Number) {
		return { value.number, value.number, value.number };
	}
	return { value.items[0].number, value.items[1].number, value.items[2].number };
}

std::vector<double> namedAxis(const Value& value, const Location& location) {
	const std::size_t index = std::string("xyz").find(value.word);
	if (value.word.size() != 1 || index == std::string::npos) {
		fail(location, "an axis is x, y, z or a vector");
		return { 0, 0, 1 };
	}
	std::vector<double> axis = { 0, 0, 0 };
	axis[index]              = 1;
	return axis;
}

std::vector<double> axisOf(const Value& value, const Location& location) {
	if (value.kind == ValueKind::Word) {
		return namedAxis(value, location);
	}
	const std::vector<double> axis = vector3(value, location);
	const double length            = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
	if (length == 0) {
		fail(location, "an axis must not be zero");
		return { 0, 0, 1 };
	}
	return { axis[0] / length, axis[1] / length, axis[2] / length };
}

Affine translation(const std::vector<double>& t) {
	return Affine {
		.linear = {
			{ 1, 0, 0 },
			{ 0, 1, 0 },
			{ 0, 0, 1 },
		},

		.offset = { t[0], t[1], t[2] },
	};
}

Affine rotation(const std::vector<double>& k, double angle) {
	const double c = std::cos(angle);
	const double s = std::sin(angle);
	const double v = 1 - c;

	return Affine {
		.linear = {
			{ c + k[0] * k[0] * v, k[0] * k[1] * v - k[2] * s, k[0] * k[2] * v + k[1] * s },
			{ k[1] * k[0] * v + k[2] * s, c + k[1] * k[1] * v, k[1] * k[2] * v - k[0] * s },
			{ k[2] * k[0] * v - k[1] * s, k[2] * k[1] * v + k[0] * s, c + k[2] * k[2] * v },
		},

		.offset = { 0, 0, 0 },
	};
}

Context scaled(const Context& context, const std::vector<double>& s, const Location& location) {
	if (s[0] == 0 || s[1] == 0 || s[2] == 0) {
		fail(location, "a scale must not be zero");
		return context;
	}

	const Affine stretched = {
		.linear = {
			{ s[0], 0, 0 },
			{ 0, s[1], 0 },
			{ 0, 0, s[2] },
		},

		.offset = { 0, 0, 0 },
	};

	Context out = composed(context, stretched);
	out.stretch *= std::min({ std::abs(s[0]), std::abs(s[1]), std::abs(s[2]) });
	return out;
}

Context painted(const Context& context, const Call& call) {
	expectCount(call, 1);
	if (failed()) {
		return context;
	}
	const Value& material = call.arguments.positional[0];
	if (material.kind != ValueKind::Material) {
		fail(call.expression.location, "paint takes a material");
		return context;
	}
	Context out  = context;
	out.material = std::int32_t(material.material);
	return out;
}

Coat blankCoat() {
	return Coat {
		.mask    = CoatMask::Pattern,
		.effect  = CoatEffect::Tint,
		.axis    = { 0, 0, 1 },
		.level   = 0,
		.width   = 0,
		.soft    = 0,
		.pattern = colourAlbedo({}),

		.material = 0,
		.colour   = { 1, 1, 1 },
		.amount   = 0,
		.ragged   = 0,
		.grain    = 1,
		.relief   = Relief::Grain,
		.span     = 1,
		.along    = { 1, 0, 0 },
		.height   = colourAlbedo({}),
	};
}

bool isCallOf(const Expression& expression, const std::vector<std::string>& names) {
	return expression.kind == ExpressionKind::Call && std::find(names.begin(), names.end(), expression.text) != names.end();
}

float namedFloat(const Call& call, const std::string& name, float otherwise) {
	const auto found = call.arguments.named.find(name);
	return found == call.arguments.named.end() ? otherwise : floatOf(found->second, call.expression.location);
}

std::vector<float> unitFloats(const Value& value, const Location& location) {
	const std::vector<float> axis = floats(value, 3, location);
	const float size              = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
	if (!(size > 0)) {
		fail(location, "an axis has a length");
		return { 0, 0, 1 };
	}
	return { axis[0] / size, axis[1] / size, axis[2] / size };
}

std::vector<float> coatAxis(const Call& call, bool positionalAxis) {
	const auto found   = call.arguments.named.find("axis");
	const bool given   = positionalAxis && !call.arguments.positional.empty();
	const Value up     = listValue({ numberValue(0), numberValue(0), numberValue(1) });
	const Value& value = given ? call.arguments.positional[0] : (found == call.arguments.named.end() ? up : found->second);
	return unitFloats(value, call.expression.location);
}

std::vector<float> namedUnit(const Call& call, const std::string& name, const std::vector<float>& otherwise) {
	const auto found = call.arguments.named.find(name);
	return found == call.arguments.named.end() ? otherwise : unitFloats(found->second, call.expression.location);
}

void checkNamed(const Call& call, const std::vector<std::string>& known) {
	for (const auto& [name, value] : call.arguments.named) {
		if (std::find(known.begin(), known.end(), name) == known.end()) {
			fail(call.expression.location, call.expression.text + " has no field '" + name + "'");
		}
	}
}

CoatEffect effectOf(const std::string& name) {
	static const std::map<std::string, CoatEffect> effects = {
		{ "tint", CoatEffect::Tint },
		{ "shade", CoatEffect::Shade },
		{ "rough", CoatEffect::Rough },
		{ "polish", CoatEffect::Rough },
		{ "wet", CoatEffect::Wet },
		{ "glow", CoatEffect::Glow },
	};

	return effects.at(name);
}

Coat namedEffect(const Coat& base, const Call& call) {
	const std::string& name = call.expression.text;
	checkNamed(call, { "strength" });
	Coat coat   = base;
	coat.effect = effectOf(name);
	if (name == "tint" || name == "glow") {
		coat.colour = colourFromFloats(floats(positional(call, 0), 3, call.expression.location));
		coat.amount = namedFloat(call, "strength", 1);
		return coat;
	}
	const float amount = floatOf(positional(call, 0), call.expression.location);
	coat.amount        = name == "polish" ? -amount : amount;
	return coat;
}

Relief reliefOf(const Value& value, const Location& location) {
	static const std::map<std::string, Relief> reliefs = {
		{ "grain", Relief::Grain },
		{ "pits", Relief::Pits },
		{ "streak", Relief::Streak },
	};

	const auto found = reliefs.find(value.word);
	if (value.kind != ValueKind::Word || found == reliefs.end()) {
		fail(location, "a bump's relief is grain, pits, streak or a function");
		return Relief::Grain;
	}
	return found->second;
}

Coat bumpEffect(Evaluator& evaluator, const Coat& base, const Call& call) {
	checkNamed(call, { "span", "along" });
	const Value relief = positional(call, 0);
	const bool pattern = relief.kind == ValueKind::Pattern;
	Coat coat          = base;
	coat.effect        = CoatEffect::Bump;
	coat.relief        = pattern ? Relief::Pattern : reliefOf(relief, call.expression.location);
	coat.height        = pattern ? patternAlbedo(evaluator, *relief.pattern) : coat.height;
	coat.amount        = floatOf(positional(call, 1), call.expression.location);
	coat.span          = namedFloat(call, "span", 1);
	coat.along         = math::vectorFromFloats(namedUnit(call, "along", { 1, 0, 0 }));
	if (!pattern && !(coat.span > 0)) {
		fail(call.expression.location, "a bump's span is more than nothing");
	}
	return coat;
}

Coat withEffect(Evaluator& evaluator, const Coat& base, const Expression& expression) {
	if (isCallOf(expression, { "bump" })) {
		const Arguments arguments = evaluateArguments(evaluator, expression);

		const Call call = {
			.arguments  = arguments,
			.expression = expression,
		};

		return bumpEffect(evaluator, base, call);
	}
	if (isCallOf(expression, { "tint", "shade", "rough", "polish", "wet", "glow" })) {
		const Arguments arguments = evaluateArguments(evaluator, expression);

		const Call call = {
			.arguments  = arguments,
			.expression = expression,
		};

		return namedEffect(base, call);
	}
	const Value value = evaluateExpression(evaluator, expression);
	if (value.kind != ValueKind::Material) {
		fail(expression.location, "a coat does tint, shade, rough, polish, wet, glow or a material");
		return base;
	}
	Coat coat     = base;
	coat.effect   = CoatEffect::Paint;
	coat.material = value.material;
	return coat;
}

Albedo maskPattern(Evaluator& evaluator, const Value& value, const Location& location) {
	if (value.kind != ValueKind::Pattern) {
		fail(location, "a coat's mask reads a function");
		return colourAlbedo({ 0, 0, 0 });
	}
	return patternAlbedo(evaluator, *value.pattern);
}

Coat raggedMask(const Coat& base, const Call& call) {
	Coat coat   = base;
	coat.ragged = namedFloat(call, "ragged", 0);
	coat.grain  = namedFloat(call, "grain", 1);
	return coat;
}

Coat levelMask(const Coat& base, const Call& call) {
	const bool facing = call.expression.text == "facing";
	const float sign  = call.expression.text == "below" ? -1.0f : 1.0f;
	checkNamed(call, { "soft", "axis", "level", "ragged", "grain" });
	const std::vector<float> axis = coatAxis(call, facing);
	Coat coat                     = raggedMask(base, call);
	coat.axis                     = math::vectorFromFloats(axis) * double(sign);
	coat.mask                     = facing ? CoatMask::Facing : CoatMask::Above;
	coat.level                    = facing ? namedFloat(call, "level", 0.5f) : sign * floatOf(positional(call, 0), call.expression.location);
	coat.soft                     = namedFloat(call, "soft", facing ? 0.2f : 0.0f);
	return coat;
}

Coat shapeMask(Evaluator& evaluator, const Coat& base, const Call& call) {
	const bool along = call.expression.text == "along";
	checkNamed(call, { "soft", "ragged", "grain" });
	Coat coat          = raggedMask(base, call);
	coat.mask          = along ? CoatMask::Along : CoatMask::Inside;
	coat.pattern       = maskPattern(evaluator, positional(call, 0), call.expression.location);
	coat.pattern.world = true;
	coat.width         = along ? floatOf(positional(call, 1), call.expression.location) : 0.0f;
	coat.soft          = namedFloat(call, "soft", 0);
	return coat;
}

Coat withMask(Evaluator& evaluator, const Coat& base, const Expression& expression) {
	if (isCallOf(expression, { "above", "below", "facing", "inside", "along" })) {
		const Arguments arguments = evaluateArguments(evaluator, expression);

		const Call call = {
			.arguments  = arguments,
			.expression = expression,
		};

		return isCallOf(expression, { "inside", "along" }) ? shapeMask(evaluator, base, call) : levelMask(base, call);
	}
	Coat coat    = base;
	coat.mask    = CoatMask::Pattern;
	coat.pattern = maskPattern(evaluator, evaluateExpression(evaluator, expression), expression.location);
	return coat;
}

bool unnamedItems(const Expression& modifier) {
	return std::all_of(modifier.names.begin(), modifier.names.end(), [](const std::string& name) {
		return name.empty();
	});
}

Coat everywhere(const Coat& base) {
	Coat coat = base;
	coat.mask = CoatMask::Everywhere;
	return coat;
}

Context coated(Evaluator& evaluator, const Context& context, const Expression& modifier) {
	const bool bare = modifier.items.size() == 1 && isCallOf(modifier.items[0], { "bump" });
	if ((modifier.items.size() != 2 && !bare) || !unnamedItems(modifier)) {
		fail(modifier.location, "coat takes an effect and a mask; a bump may leave the mask out");
		return context;
	}
	const Coat effect = withEffect(evaluator, blankCoat(), modifier.items[0]);
	evaluator.looks.coats.push_back(bare ? everywhere(effect) : withMask(evaluator, effect, modifier.items[1]));
	Context out = context;
	out.coats.insert(out.coats.begin(), std::uint32_t(evaluator.looks.coats.size() - 1));
	return out;
}

Context turned(const Context& context, const Call& call) {
	expectCount(call, 2);
	if (failed()) {
		return context;
	}
	const std::vector<double> axis = axisOf(call.arguments.positional[0], call.expression.location);
	return composed(context, rotation(axis, floatOf(call.arguments.positional[1], call.expression.location)));
}

std::uint64_t groupBit(Looks& looks, const std::string& name, const Location& location) {
	const auto found = std::find(looks.groups.begin(), looks.groups.end(), name);
	if (found != looks.groups.end()) {
		return std::uint64_t(1) << (found - looks.groups.begin());
	}
	if (looks.groups.size() >= mostGroups) {
		fail(location, "a scene names at most 64 groups");
		return 0;
	}
	looks.groups.push_back(name);
	return std::uint64_t(1) << (looks.groups.size() - 1);
}

std::uint8_t ruleBit(const std::string& name, const Location& location) {
	static const std::map<std::string, Rule> rules = {
		{ "camera", Rule::Hidden },
		{ "shadows", Rule::Shadowless },
		{ "caustics", Rule::Causticless },
	};

	if (!rules.contains(name)) {
		fail(location, "a rule is camera, shadows or caustics");
		return 0;
	}
	return std::uint8_t(rules.at(name));
}

Context ruled(const Context& context, const Call& call) {
	if (!call.arguments.positional.empty() || call.arguments.named.empty()) {
		fail(call.expression.location, "rule takes named switches: camera, shadows or caustics");
		return context;
	}
	Context out = context;
	for (const auto& [name, value] : call.arguments.named) {
		const std::uint8_t bit = ruleBit(name, call.expression.location);
		const bool refused     = floatOf(value, call.expression.location) == 0;
		out.rules              = refused ? std::uint8_t(out.rules | bit) : std::uint8_t(out.rules & ~bit);
	}
	return out;
}

Context grouped(Evaluator& evaluator, const Context& context, const Call& call) {
	expectCount(call, 1);
	if (failed()) {
		return context;
	}
	const Value& name = call.arguments.positional[0];
	if (name.kind != ValueKind::Word) {
		fail(call.expression.location, "group takes a name");
		return context;
	}
	Context out = context;
	out.groups |= groupBit(evaluator.looks, name.word, call.expression.location);
	return out;
}

Context modified(Evaluator& evaluator, const Context& context, const Expression& modifier) {
	if (modifier.text == "coat") {
		return coated(evaluator, context, modifier);
	}
	const Arguments arguments = evaluateArguments(evaluator, modifier);

	const Call call = {
		.arguments  = arguments,
		.expression = modifier,
	};

	if (modifier.text == "turn") {
		return turned(context, call);
	}
	if (modifier.text == "paint") {
		return painted(context, call);
	}
	if (modifier.text == "rule") {
		return ruled(context, call);
	}
	if (modifier.text == "group") {
		return grouped(evaluator, context, call);
	}
	expectCount(call, 1);
	if (failed()) {
		return context;
	}
	const std::vector<double> v = vector3(arguments.positional[0], modifier.location);
	if (modifier.text == "scale") {
		return scaled(context, v, modifier.location);
	}
	return composed(context, translation(v));
}

double determinant(const double m[3][3]) {
	const double a = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]);
	const double b = m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]);
	const double c = m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	return a - b + c;
}

double inverseEntry(const double m[3][3], int i, int j) {
	const int firstRow     = (j + 1) % 3;
	const int secondRow    = (j + 2) % 3;
	const int firstColumn  = (i + 1) % 3;
	const int secondColumn = (i + 2) % 3;
	return (m[firstRow][firstColumn] * m[secondRow][secondColumn] - m[firstRow][secondColumn] * m[secondRow][firstColumn]) / determinant(m);
}

Placement placementOf(const Context& context) {
	Placement placement = {
		.linear = {
			{ 0, 0, 0 },
			{ 0, 0, 0 },
			{ 0, 0, 0 },
		},

		.origin  = { 0, 0, 0 },
		.stretch = context.stretch,
	};

	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			placement.linear[i][j] = inverseEntry(context.linear, i, j);
		}
		placement.origin[i] = context.origin[i];
	}
	return placement;
}

std::uint32_t defaultMaterial(Evaluator& evaluator) {
	if (evaluator.defaultMaterial < 0) {
		const Material grey = {
			.kind       = MaterialKind::Matte,
			.albedo     = colourAlbedo({ 0.8f, 0.8f, 0.8f }),
			.roughness  = 0.5f,
			.refraction = 1.5f,
			.dispersion = 0,
			.scatter    = 0,
			.lean       = 0,
			.absorb     = { 0, 0, 0 },
			.glow       = { 0, 0, 0 },
			.strength   = 0,
			.film       = { 0, 1 },
			.guided     = true,
			.relays     = true,
			.aimed      = true,
		};

		const Value pushed = pushMaterial(evaluator, grey);

		evaluator.defaultMaterial = std::int32_t(pushed.material);
	}
	return std::uint32_t(evaluator.defaultMaterial);
}

std::uint32_t pushNode(Scene& scene, const Node& node) {
	scene.nodes.push_back(node);
	return std::uint32_t(scene.nodes.size() - 1);
}

void sinkNode(Evaluator& evaluator, const Context& context, std::uint32_t node) {
	std::vector<std::uint32_t>& sink = context.sink ? *context.sink : evaluator.scene.objects;
	sink.push_back(node);
}

std::uint32_t materialOf(Evaluator& evaluator, const Context& context) {
	return context.material < 0 ? defaultMaterial(evaluator) : std::uint32_t(context.material);
}

void emitLeaf(Evaluator& evaluator, const Pattern& pattern, const Context& context) {
	if (!pattern.stops.empty() || pattern.world) {
		fail(pattern.location, "a ramp or world pattern is no shape");
		return;
	}

	const Leaf leaf = {
		.program   = specialise(evaluator, pattern),
		.arguments = std::uint32_t(evaluator.scene.arguments.size()),
		.placement = placementOf(context),
		.material  = materialOf(evaluator, context),
		.coats     = context.coats,
		.rules     = context.rules,
		.groups    = context.groups,
	};

	pushArguments(evaluator, pattern.arguments);
	evaluator.scene.leaves.push_back(leaf);
	const std::uint32_t index = std::uint32_t(evaluator.scene.leaves.size() - 1);

	const Node node = {
		.join     = Join::Leaf,
		.radius   = 0,
		.leaf     = index,
		.children = {},
	};

	sinkNode(evaluator, context, pushNode(evaluator.scene, node));
}

struct Group {
	Join join;
	double radius;
	const std::vector<Statement>& body;
	Location location;
};

std::vector<std::uint32_t> collectChildren(Evaluator& evaluator, const std::vector<Statement>& body, const Context& context) {
	std::vector<std::uint32_t> children;
	Context inner = context;
	inner.sink    = &children;
	evaluator.scopes.push_back(Scope {});
	runBlock(evaluator, body, inner);
	evaluator.scopes.pop_back();
	return children;
}

void emitGroup(Evaluator& evaluator, const Group& group, const Context& context) {
	const std::vector<std::uint32_t> children = collectChildren(evaluator, group.body, context);
	if (children.empty()) {
		fail(group.location, "this emits no shape");
		return;
	}
	if (children.size() == 1 && group.join == Join::Merge) {
		sinkNode(evaluator, context, children[0]);
		return;
	}

	const Node joined = {
		.join     = group.join,
		.radius   = group.radius,
		.leaf     = 0,
		.children = children,
	};

	sinkNode(evaluator, context, pushNode(evaluator.scene, joined));
}

struct Fields {
	std::map<std::string, Value> values;
	const Statement& setting;
};

bool hasField(const Fields& fields, const std::string& name) {
	return fields.values.contains(name);
}

void readColour(const Fields& fields, const std::string& name, Colour& into) {
	if (hasField(fields, name)) {
		into = colourFromFloats(floats(fields.values.at(name), 3, fields.setting.location));
	}
}

void readVector(const Fields& fields, const std::string& name, math::Vector& into) {
	if (hasField(fields, name)) {
		into = math::vectorFromFloats(floats(fields.values.at(name), 3, fields.setting.location));
	}
}

void readFloat(const Fields& fields, const std::string& name, float& into) {
	if (hasField(fields, name)) {
		into = floatOf(fields.values.at(name), fields.setting.location);
	}
}

void readInt(const Fields& fields, const std::string& name, int& into) {
	if (hasField(fields, name)) {
		into = int(floatOf(fields.values.at(name), fields.setting.location));
	}
}

void checkFields(const Fields& fields, const std::vector<std::string>& known) {
	for (const auto& [name, value] : fields.values) {
		if (std::find(known.begin(), known.end(), name) == known.end()) {
			fail(fields.setting.location, fields.setting.text + " has no field '" + name + "'");
		}
	}
}

bool splitNamed(int split) {
	return split == 0 || split == 1 || split == 2 || split == 4 || split == 8;
}

void readRender(Shot& shot, const Fields& fields) {
	checkFields(fields, { "width", "height", "tolerance", "seed", "far", "samples", "batch", "split", "surfaces", "collisions" });
	readInt(fields, "width", shot.render.width);
	readInt(fields, "height", shot.render.height);
	readFloat(fields, "tolerance", shot.render.tolerance);
	readInt(fields, "seed", shot.render.seed);
	readFloat(fields, "far", shot.render.far);
	readInt(fields, "samples", shot.render.samples);
	readInt(fields, "batch", shot.render.batch);
	readInt(fields, "split", shot.render.split);
	readInt(fields, "surfaces", shot.render.surfaces);
	readInt(fields, "collisions", shot.render.collisions);
	if (shot.render.surfaces < 1) {
		fail(fields.setting.location, "a render's surfaces must be at least one");
	}
	if (shot.render.collisions < 1) {
		fail(fields.setting.location, "a render's collisions must be at least one");
	}
	if (!splitNamed(shot.render.split)) {
		fail(fields.setting.location, "a render's split must be 0, 1, 2, 4 or 8");
	}
}

void readCamera(Shot& shot, const Fields& fields) {
	checkFields(fields, { "eye", "target", "up", "fov", "aperture", "focus" });
	readVector(fields, "eye", shot.camera.eye);
	readVector(fields, "target", shot.camera.target);
	readVector(fields, "up", shot.camera.up);
	readFloat(fields, "fov", shot.camera.fov);
	readFloat(fields, "aperture", shot.camera.aperture);
	readFloat(fields, "focus", shot.camera.focus);
}

std::vector<double> clipCuts(const Fields& fields, double duration) {
	const Value& cuts = fields.values.at("cuts");
	if (cuts.kind != ValueKind::List) {
		fail(fields.setting.location, "cuts is a list of times");
		return {};
	}
	std::vector<double> times;
	for (const Value& cut : cuts.items) {
		times.push_back(knownOf(cut, fields.setting.location));
		const bool rising = times.size() < 2 || times.back() > times[times.size() - 2];
		if (!rising || !(times.back() > 0 && times.back() < duration)) {
			fail(fields.setting.location, "a clip's cuts rise and lie inside it");
			return {};
		}
	}
	return times;
}

void readClip(Shot& shot, const Fields& fields) {
	checkFields(fields, { "duration", "rate", "cuts" });
	if (!hasField(fields, "duration")) {
		fail(fields.setting.location, "a clip has a duration");
		return;
	}
	shot.clip.duration = knownOf(fields.values.at("duration"), fields.setting.location);
	shot.clip.rate     = hasField(fields, "rate") ? knownOf(fields.values.at("rate"), fields.setting.location) : shot.clip.rate;
	if (!(shot.clip.duration > 0) || !(shot.clip.rate > 0)) {
		fail(fields.setting.location, "a clip's duration and rate are above 0");
		return;
	}
	shot.clip.cuts = hasField(fields, "cuts") ? clipCuts(fields, shot.clip.duration) : std::vector<double> {};
}

Curve curveOf(const Fields& fields) {
	const std::string& curve = fields.values.at("curve").word;
	if (curve != "filmic" && curve != "clamped") {
		fail(fields.setting.location, "a curve is filmic or clamped");
	}
	return curve == "filmic" ? Curve::Filmic : Curve::Clamped;
}

void readGrade(Shot& shot, const Fields& fields) {
	checkFields(fields, { "exposure", "curve" });
	readFloat(fields, "exposure", shot.grade.exposure);
	if (hasField(fields, "curve")) {
		shot.grade.curve = curveOf(fields);
	}
}

SkyKind skyKind(const Fields& fields) {
	static const std::map<std::string, SkyKind> kinds = {
		{ "gradient", SkyKind::Gradient },
		{ "physical", SkyKind::Physical },
		{ "painted", SkyKind::Painted },
	};

	if (!kinds.contains(fields.setting.word)) {
		fail(fields.setting.location, "a sky is gradient, physical or painted");
		return SkyKind::Gradient;
	}
	return kinds.at(fields.setting.word);
}

SkyStop skyStopOf(const Value& value, const Location& location) {
	const std::vector<float> numbers = floats(value, 4, location);
	if (numbers[0] < -1 || numbers[0] > 1) {
		fail(location, "a stop's height is from -1 to 1");
	}
	return SkyStop {
		.height = numbers[0],
		.colour = { numbers[1], numbers[2], numbers[3] },
	};
}

void readStops(const Fields& fields, const std::string& name, std::vector<SkyStop>& into) {
	if (!hasField(fields, name)) {
		return;
	}
	const Value& run = fields.values.at(name);
	if (run.kind != ValueKind::List || run.items.size() < 2) {
		fail(fields.setting.location, "a run of a painted sky is two stops or more");
		return;
	}
	into.clear();
	for (const Value& item : run.items) {
		into.push_back(skyStopOf(item, fields.setting.location));
		if (into.size() > 1 && !(into.back().height > into[into.size() - 2].height)) {
			fail(fields.setting.location, "a painted sky's stops rise from one to the next");
			return;
		}
	}
}

void readPainted(Lighting& lighting, const Fields& fields) {
	checkFields(fields, { "day", "dusk", "night", "glow", "strength", "spread" });
	readStops(fields, "day", lighting.sky.painted.day);
	readStops(fields, "dusk", lighting.sky.painted.dusk);
	readStops(fields, "night", lighting.sky.painted.night);
}

void readSkyGlow(Lighting& lighting, const Fields& fields) {
	readColour(fields, "glow", lighting.sky.glow);
	readFloat(fields, "strength", lighting.sky.strength);
	readFloat(fields, "spread", lighting.sky.spread);
}

void readSky(Lighting& lighting, const Fields& fields) {
	lighting.sky.kind = skyKind(fields);
	readSkyGlow(lighting, fields);
	if (lighting.sky.kind == SkyKind::Painted) {
		readPainted(lighting, fields);
		return;
	}
	checkFields(fields, { "zenith", "horizon", "ground", "haze", "glow", "strength", "spread" });
	readColour(fields, "zenith", lighting.sky.zenith);
	readColour(fields, "horizon", lighting.sky.horizon);
	readColour(fields, "ground", lighting.sky.ground);
	readFloat(fields, "haze", lighting.sky.haze);
}

void readClouds(Lighting& lighting, const Fields& fields) {
	checkFields(fields, { "height", "thickness", "cover", "density", "span" });
	lighting.clouds.present = true;
	readFloat(fields, "height", lighting.clouds.height);
	readFloat(fields, "thickness", lighting.clouds.thickness);
	readFloat(fields, "cover", lighting.clouds.cover);
	readFloat(fields, "density", lighting.clouds.density);
	readFloat(fields, "span", lighting.clouds.span);
}

void readStars(Lighting& lighting, const Fields& fields) {
	checkFields(fields, { "count", "strength", "angle", "axis" });
	lighting.stars.present = true;
	readFloat(fields, "count", lighting.stars.count);
	readFloat(fields, "strength", lighting.stars.strength);
	readFloat(fields, "angle", lighting.stars.angle);
	readVector(fields, "axis", lighting.stars.axis);
	if (!(lighting.stars.count >= 1)) {
		fail(fields.setting.location, "stars number one or more");
	}
}

std::uint64_t reachedGroups(Looks& looks, const Fields& fields) {
	const Value& names = fields.values.at("reaches");
	if (names.kind != ValueKind::List) {
		fail(fields.setting.location, "reaches is a list of group names");
		return 0;
	}
	std::uint64_t reached = 0;
	for (const Value& name : names.items) {
		if (name.kind != ValueKind::Word) {
			fail(fields.setting.location, "reaches is a list of group names");
			return 0;
		}
		reached |= groupBit(looks, name.word, fields.setting.location);
	}
	return reached;
}

LightKind lightKind(const Fields& fields) {
	static const std::map<std::string, LightKind> kinds = {
		{ "point", LightKind::Point },
		{ "directional", LightKind::Directional },
		{ "sphere", LightKind::Sphere },
		{ "quad", LightKind::Quad },
		{ "sun", LightKind::Sun },
		{ "moon", LightKind::Moon },
	};

	if (!kinds.contains(fields.setting.word)) {
		fail(fields.setting.location, "a light is point, directional, sphere, quad, sun or moon");
		return LightKind::Point;
	}
	return kinds.at(fields.setting.word);
}

void readLight(Lighting& lighting, Looks& looks, const Fields& fields) {
	checkFields(fields, { "at", "direction", "along", "across", "radius", "angle", "power", "reaches", "caustics" });

	Light light = {
		.kind      = lightKind(fields),
		.at        = { 0, 0, 0 },
		.direction = { 0, 0, 1 },
		.along     = { 1, 0, 0 },
		.across    = { 0, 1, 0 },
		.radius    = 0,
		.angle     = 0.0093f,
		.power     = { 1, 1, 1 },
		.reaches   = 0,
		.caustics  = true,
	};

	readVector(fields, "at", light.at);
	readVector(fields, "direction", light.direction);
	readVector(fields, "along", light.along);
	readVector(fields, "across", light.across);
	readFloat(fields, "radius", light.radius);
	readFloat(fields, "angle", light.angle);
	readColour(fields, "power", light.power);
	light.reaches  = hasField(fields, "reaches") ? reachedGroups(looks, fields) : 0;
	light.caustics = !hasField(fields, "caustics") || floatOf(fields.values.at("caustics"), fields.setting.location) != 0;
	lighting.lights.push_back(light);
}

Fields evaluateFields(Evaluator& evaluator, const Statement& setting) {
	Fields fields = {
		.values  = {},
		.setting = setting,
	};

	for (std::size_t i = 0; i < setting.names.size(); ++i) {
		fields.values[setting.names[i]] = evaluateExpression(evaluator, setting.expressions[i]);
	}
	return fields;
}

void readShotSetting(Shot& shot, const std::string& name, const Fields& fields) {
	using Reader = void (*)(Shot&, const Fields&);

	static const std::map<std::string, Reader> readers = {
		{ "render", readRender },
		{ "camera", readCamera },
		{ "grade", readGrade },
		{ "clip", readClip },
	};

	readers.at(name)(shot, fields);
}

void readLightingSetting(Lighting& lighting, const std::string& name, const Fields& fields) {
	using Reader = void (*)(Lighting&, const Fields&);

	static const std::map<std::string, Reader> readers = {
		{ "sky", readSky },
		{ "clouds", readClouds },
		{ "stars", readStars },
	};

	readers.at(name)(lighting, fields);
}

bool shotSetting(const std::string& name) {
	return name == "render" || name == "camera" || name == "grade" || name == "clip";
}

void runSetting(Evaluator& evaluator, const Statement& setting) {
	const Fields fields = evaluateFields(evaluator, setting);
	if (setting.text == "light") {
		readLight(evaluator.lighting, evaluator.looks, fields);
		return;
	}
	if (shotSetting(setting.text)) {
		readShotSetting(evaluator.shot, setting.text, fields);
		return;
	}
	readLightingSetting(evaluator.lighting, setting.text, fields);
}

void runFile(Evaluator& evaluator, const std::string& path, const Location& location);

std::string directoryOf(const std::string& path) {
	const std::size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

std::string importedPath(const std::string& from, const std::string& path) {
	return path.starts_with('/') ? path : directoryOf(from) + path;
}

struct LoopLevel {
	const Statement& loop;
	std::size_t level;
};

struct LoopItem {
	const Statement& loop;
	std::size_t level;
	Value value;
};

void runLoopLevel(Evaluator& evaluator, const LoopLevel& at, const Context& context);

void runLoopItem(Evaluator& evaluator, const LoopItem& item, const Context& context) {
	const Scope bound = {
		{ item.loop.names[item.level], item.value },
	};

	evaluator.scopes.push_back(bound);
	if (item.level + 1 == item.loop.names.size()) {
		runBlock(evaluator, item.loop.body, context);
	}
	if (item.level + 1 < item.loop.names.size()) {
		const LoopLevel level = {
			.loop  = item.loop,
			.level = item.level + 1,
		};

		runLoopLevel(evaluator, level, context);
	}
	evaluator.scopes.pop_back();
}

std::vector<Value> rangeValues(Evaluator& evaluator, const Expression& range) {
	const Value from = evaluateExpression(evaluator, range.items[0]);
	const Value to   = evaluateExpression(evaluator, range.items[1]);
	if (!isKnown(from) || !isKnown(to)) {
		fail(range.location, "a loop's bounds must be known when the scene loads");
		return {};
	}
	std::vector<Value> values;
	for (double i = from.number; i < to.number; i += 1) {
		values.push_back(numberValue(i));
	}
	return values;
}

std::vector<Value> loopValues(Evaluator& evaluator, const Expression& source) {
	if (source.kind == ExpressionKind::Binary && source.text == "..") {
		return rangeValues(evaluator, source);
	}
	const Value list = evaluateExpression(evaluator, source);
	if (list.kind != ValueKind::List) {
		fail(source.location, "a loop runs over a range or a list");
		return {};
	}
	return list.items;
}

void runLoopLevel(Evaluator& evaluator, const LoopLevel& at, const Context& context) {
	for (const Value& value : loopValues(evaluator, at.loop.expressions[at.level])) {
		if (failed()) {
			return;
		}

		const LoopItem item = {
			.loop  = at.loop,
			.level = at.level,
			.value = value,
		};

		runLoopItem(evaluator, item, context);
	}
}

void runLoop(Evaluator& evaluator, const Statement& loop, const Context& context) {
	const LoopLevel first = {
		.loop  = loop,
		.level = 0,
	};

	runLoopLevel(evaluator, first, context);
}

void runScoped(Evaluator& evaluator, const std::vector<Statement>& body, const Context& context) {
	evaluator.scopes.push_back(Scope {});
	runBlock(evaluator, body, context);
	evaluator.scopes.pop_back();
}

void mergeBranch(Evaluator& evaluator, const std::vector<Scope>& before, const Operation& select) {
	for (std::size_t i = 0; i < before.size(); ++i) {
		for (auto& [name, after] : evaluator.scopes[i]) {
			const auto old = before[i].find(name);
			if (old != before[i].end() && !sameValue(old->second, after)) {
				const Operation operands = {
					.opcode   = Opcode::Select,
					.a        = select.a,
					.b        = after,
					.c        = old->second,
					.location = select.location,
				};

				after = selectValue(evaluator, operands);
			}
		}
	}
}

void runUnknownIf(Evaluator& evaluator, const Statement& branch, const Value& condition) {
	const std::vector<Scope> before = evaluator.scopes;
	runScoped(evaluator, branch.body, rootContext());
	if (evaluator.compiler->returned) {
		fail(branch.location, "a function may not return inside an if it cannot decide when loading");
	}

	const Operation operands = {
		.opcode   = Opcode::Select,
		.a        = condition,
		.b        = numberValue(0),
		.c        = numberValue(0),
		.location = branch.location,
	};

	mergeBranch(evaluator, before, operands);
}

void runIf(Evaluator& evaluator, const Statement& branch, const Context& context) {
	const Value condition = evaluateExpression(evaluator, branch.expressions[0]);
	expectNumber(condition, branch.location);
	if (failed()) {
		return;
	}
	if (!isKnown(condition)) {
		runUnknownIf(evaluator, branch, condition);
		return;
	}
	if (condition.number != 0) {
		runScoped(evaluator, branch.body, context);
	}
}

void expectBuilding(const Evaluator& evaluator, const Statement& statement) {
	if (evaluator.compiler) {
		fail(statement.location, "a function only computes; shapes are emitted in a shape or at the top");
	}
}

const Statement* findShape(const Evaluator& evaluator, const Expression& call) {
	const auto found   = evaluator.definitions.find(call.text);
	const bool isShape = found != evaluator.definitions.end() && found->second->kind == StatementKind::Shape;
	return isShape ? found->second : nullptr;
}

struct ShapeCall {
	const Statement& shape;
	const Expression& call;
};

void runShape(Evaluator& evaluator, const ShapeCall& shapeCall, const Context& context);

void runCall(Evaluator& evaluator, const Statement& statement, const Context& context) {
	expectBuilding(evaluator, statement);
	if (failed()) {
		return;
	}
	const Expression& call = statement.expressions[0];
	if (const Statement* shape = findShape(evaluator, call)) {
		const ShapeCall shapeCall = {
			.shape = *shape,
			.call  = call,
		};

		runShape(evaluator, shapeCall, context);
		return;
	}
	const Value value = evaluateExpression(evaluator, call);
	if (failed()) {
		return;
	}
	if (value.kind != ValueKind::Pattern) {
		fail(call.location, "only a function or a shape can stand as a statement");
		return;
	}
	emitLeaf(evaluator, *value.pattern, context);
}

void runShape(Evaluator& evaluator, const ShapeCall& shapeCall, const Context& context) {
	const Arguments arguments = evaluateArguments(evaluator, shapeCall.call);
	std::vector<std::uint32_t> children;
	Context inner = context;
	inner.sink    = &children;

	const Invocation invocation = {
		.definition = shapeCall.shape,
		.arguments  = arguments.positional,
		.context    = inner,
		.location   = shapeCall.call.location,
	};

	runDefinition(evaluator, invocation);
	if (children.empty()) {
		return;
	}

	const Node merged = {
		.join     = Join::Merge,
		.radius   = 0,
		.leaf     = 0,
		.children = children,
	};

	const std::uint32_t node = children.size() == 1 ? children[0] : pushNode(evaluator.scene, merged);

	sinkNode(evaluator, context, node);
}

Context modifiedBy(Evaluator& evaluator, const std::vector<Expression>& modifiers, const Context& context) {
	Context inner = context;
	for (const Expression& modifier : modifiers) {
		inner = modified(evaluator, inner, modifier);
	}
	return inner;
}

void runPlace(Evaluator& evaluator, const Statement& place, const Context& context) {
	expectBuilding(evaluator, place);
	if (failed()) {
		return;
	}
	const Context inner = modifiedBy(evaluator, place.expressions, context);
	if (!context.sink) {
		runScoped(evaluator, place.body, inner);
		return;
	}

	const Group group = {
		.join     = Join::Merge,
		.radius   = 0,
		.body     = place.body,
		.location = place.location,
	};

	emitGroup(evaluator, group, inner);
}

Join joinKind(const std::string& name) {
	return name == "merge" ? Join::Merge : (name == "cut" ? Join::Cut : Join::Clip);
}

int repeatCount(double count, const Location& location) {
	if (!(count >= 1) || count != std::floor(count)) {
		fail(location, "a repeat count is a whole number of one or more");
		return 1;
	}
	return int(count);
}

Value namedArgument(const Arguments& arguments, const std::string& name, const Location& location) {
	if (!arguments.positional.empty() || arguments.named.size() != 2 || !arguments.named.contains(name)) {
		fail(location, "repeat takes count and step");
		return numberValue(1);
	}
	return arguments.named.at(name);
}

RepeatCall repeatCallOf(Evaluator& evaluator, const Expression& call, const Context& context) {
	const Arguments arguments        = evaluateArguments(evaluator, call);
	const std::vector<double> counts = vector3(namedArgument(arguments, "count", call.location), call.location);
	const std::vector<double> step   = vector3(namedArgument(arguments, "step", call.location), call.location);

	RepeatCall repeat = {
		.counts   = { 1, 1, 1 },
		.steps    = {},
		.location = call.location,
	};

	for (int axis = 0; axis < 3; ++axis) {
		repeat.counts[axis] = repeatCount(counts[std::size_t(axis)], call.location);
		for (int row = 0; row < 3; ++row) {
			repeat.steps[axis][row] = context.linear[row][axis] * step[std::size_t(axis)];
		}
	}
	return repeat;
}

std::uint32_t copiedNode(Evaluator& evaluator, std::uint32_t index, const double offset[3]) {
	Scene& scene    = evaluator.scene;
	const Node node = scene.nodes[index];
	if (node.join != Join::Leaf) {
		Node copy     = node;
		copy.children = {};
		for (const std::uint32_t child : node.children) {
			copy.children.push_back(copiedNode(evaluator, child, offset));
		}
		return pushNode(scene, copy);
	}
	Leaf leaf = scene.leaves[node.leaf];
	for (int i = 0; i < 3; ++i) {
		leaf.placement.origin[i] += offset[i];
	}
	scene.leaves.push_back(leaf);
	Node copy = node;
	copy.leaf = std::uint32_t(scene.leaves.size() - 1);
	return pushNode(scene, copy);
}

void copyOffset(const RepeatCall& call, const int at[3], double offset[3]) {
	for (int row = 0; row < 3; ++row) {
		offset[row] = call.steps[0][row] * at[0] + call.steps[1][row] * at[1] + call.steps[2][row] * at[2];
	}
}

std::vector<std::uint32_t> repeatedCopies(Evaluator& evaluator, const RepeatCall& call, const std::vector<std::uint32_t>& first) {
	std::vector<std::uint32_t> copies = first;
	for (int k = 0; k < call.counts[2]; ++k) {
		for (int j = 0; j < call.counts[1]; ++j) {
			for (int i = 0; i < call.counts[0]; ++i) {
				if (i == 0 && j == 0 && k == 0) {
					continue;
				}
				const int at[3] = { i, j, k };
				double offset[3];
				copyOffset(call, at, offset);
				for (const std::uint32_t node : first) {
					copies.push_back(copiedNode(evaluator, node, offset));
				}
			}
		}
	}
	return copies;
}

void sinkMerged(Evaluator& evaluator, const Context& context, const std::vector<std::uint32_t>& nodes) {
	if (!context.sink || nodes.size() == 1) {
		for (const std::uint32_t node : nodes) {
			sinkNode(evaluator, context, node);
		}
		return;
	}

	const Node merged = {
		.join     = Join::Merge,
		.radius   = 0,
		.leaf     = 0,
		.children = nodes,
	};

	sinkNode(evaluator, context, pushNode(evaluator.scene, merged));
}

void emitRepeated(Evaluator& evaluator, const Repetition& repetition) {
	const std::vector<std::uint32_t> first = collectChildren(evaluator, repetition.block, repetition.context);
	if (first.empty()) {
		fail(repetition.call.location, "this emits no shape");
		return;
	}
	sinkMerged(evaluator, repetition.context, repeatedCopies(evaluator, repetition.call, first));
}

void runRepeat(Evaluator& evaluator, const Statement& repeat, const Context& context) {
	const RepeatCall call = repeatCallOf(evaluator, repeat.expressions[0], context);

	const Repetition repetition = {
		.call    = call,
		.block   = repeat.body,
		.context = context,
	};

	emitRepeated(evaluator, repetition);
}

void runJoin(Evaluator& evaluator, const Statement& join, const Context& context) {
	expectBuilding(evaluator, join);
	if (failed()) {
		return;
	}
	if (join.text == "repeat") {
		runRepeat(evaluator, join, context);
		return;
	}
	const double radius = join.expressions.empty() ? 0 : floatOf(evaluateExpression(evaluator, join.expressions[0]), join.location);

	const Group group = {
		.join     = joinKind(join.text),
		.radius   = radius,
		.body     = join.body,
		.location = join.location,
	};

	emitGroup(evaluator, group, context);
}

void define(Evaluator& evaluator, const Statement& statement) {
	if (evaluator.scopes.size() != 1) {
		fail(statement.location, "function and shape are defined at the top of a file");
		return;
	}
	if (evaluator.definitions.contains(statement.text)) {
		fail(statement.location, statement.text + " is defined twice");
		return;
	}
	evaluator.definitions[statement.text] = &statement;
}

void runReturn(Evaluator& evaluator, const Statement& statement) {
	if (!evaluator.compiler) {
		fail(statement.location, "return belongs in a function");
		return;
	}
	evaluator.compiler->result   = evaluateExpression(evaluator, statement.expressions[0]);
	evaluator.compiler->returned = true;
}

void runLet(Evaluator& evaluator, const Statement& statement) {
	if (statement.text == "time") {
		fail(statement.location, "time belongs to the clip and cannot be changed");
		return;
	}
	bind(evaluator, statement.text, evaluateExpression(evaluator, statement.expressions[0]));
}

void runSettingStatement(Evaluator& evaluator, const Statement& statement) {
	expectBuilding(evaluator, statement);
	if (failed()) {
		return;
	}
	runSetting(evaluator, statement);
}

void runStatement(Evaluator& evaluator, const Statement& statement, const Context& context) {
	if (failed()) {
		return;
	}
	if (evaluator.compiler && evaluator.compiler->returned) {
		fail(statement.location, "nothing may follow a return");
		return;
	}
	switch (statement.kind) {
		case StatementKind::Import: return runFile(evaluator, importedPath(*statement.location.file, statement.text), statement.location);
		case StatementKind::Let: return runLet(evaluator, statement);
		case StatementKind::For: return runLoop(evaluator, statement, context);
		case StatementKind::If: return runIf(evaluator, statement, context);
		case StatementKind::Return: return runReturn(evaluator, statement);
		case StatementKind::Function: return define(evaluator, statement);
		case StatementKind::Shape: return define(evaluator, statement);
		case StatementKind::Setting: return runSettingStatement(evaluator, statement);
		case StatementKind::Place: return runPlace(evaluator, statement, context);
		case StatementKind::Join: return runJoin(evaluator, statement, context);
		case StatementKind::Call: return runCall(evaluator, statement, context);
	}
}

void runBlock(Evaluator& evaluator, const std::vector<Statement>& body, const Context& context) {
	for (const Statement& statement : body) {
		runStatement(evaluator, statement, context);
	}
}

std::string readText(const std::string& path, const Location& location) {
	std::ifstream in(path);
	if (!in) {
		fail(location, "cannot read " + path);
		return {};
	}
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

void runFile(Evaluator& evaluator, const std::string& path, const Location& location) {
	if (evaluator.imported.contains(path)) {
		return;
	}
	evaluator.imported.insert(path);
	const std::string text = readText(path, location);
	if (failed()) {
		return;
	}
	evaluator.files.push_back(path);
	evaluator.trees.push_back(parseSource(text, &evaluator.files.back()));
	runBlock(evaluator, evaluator.trees.back(), rootContext());
}

Context rootContext() {
	return Context {
		.linear = {
			{ 1, 0, 0 },
			{ 0, 1, 0 },
			{ 0, 0, 1 },
		},

		.origin   = { 0, 0, 0 },
		.stretch  = 1,
		.material = -1,
		.coats    = {},
		.rules    = 0,
		.groups   = 0,
		.sink     = nullptr,
	};
}

Render defaultRender() {
	return Render {
		.width      = 960,
		.height     = 600,
		.tolerance  = 2,
		.seed       = 1,
		.far        = 1000,
		.samples    = 0,
		.batch      = 0,
		.split      = 0,
		.surfaces   = 10,
		.collisions = 256,
	};
}

Camera defaultCamera() {
	return Camera {
		.eye      = { 0, -5, 1 },
		.target   = { 0, 0, 0 },
		.up       = { 0, 0, 1 },
		.fov      = 0.8f,
		.aperture = 0,
		.focus    = 0,
	};
}

Grade defaultGrade() {
	return Grade {
		.exposure = 1,
		.curve    = Curve::Filmic,
	};
}

std::vector<SkyStop> stopsOf(const std::vector<std::array<float, 4>>& rows) {
	std::vector<SkyStop> stops;
	for (const std::array<float, 4>& row : rows) {
		const SkyStop stop = {
			.height = row[0],
			.colour = { row[1], row[2], row[3] },
		};

		stops.push_back(stop);
	}
	return stops;
}

PaintedSky defaultPainted() {
	const std::vector<std::array<float, 4>> day = {
		{ -1, 0.05f, 0.045f, 0.04f },
		{ -0.02f, 0.10f, 0.09f, 0.08f },
		{ 0, 0.22f, 0.32f, 0.50f },
		{ 0.12f, 0.06f, 0.16f, 0.45f },
		{ 1, 0.01f, 0.05f, 0.30f },
	};

	const std::vector<std::array<float, 4>> dusk = {
		{ -1, 0.015f, 0.01f, 0.01f },
		{ -0.02f, 0.05f, 0.025f, 0.015f },
		{ 0, 0.60f, 0.22f, 0.05f },
		{ 0.08f, 0.40f, 0.12f, 0.07f },
		{ 0.30f, 0.08f, 0.05f, 0.14f },
		{ 1, 0.012f, 0.02f, 0.08f },
	};

	const std::vector<std::array<float, 4>> night = {
		{ -1, 0.002f, 0.002f, 0.003f },
		{ 0, 0.010f, 0.014f, 0.030f },
		{ 1, 0.002f, 0.004f, 0.012f },
	};

	return PaintedSky {
		.day   = stopsOf(day),
		.dusk  = stopsOf(dusk),
		.night = stopsOf(night),
	};
}

Sky defaultSky() {
	return Sky {
		.kind     = SkyKind::Gradient,
		.zenith   = { 0.05f, 0.13f, 0.34f },
		.horizon  = { 0.26f, 0.40f, 0.62f },
		.ground   = { 0.10f, 0.09f, 0.08f },
		.haze     = 3,
		.glow     = { 0, 0, 0 },
		.strength = 1,
		.spread   = 0.12f,
		.painted  = defaultPainted(),
	};
}

Stars defaultStars() {
	return Stars {
		.present  = false,
		.count    = 3000,
		.strength = 2,
		.angle    = 0.003f,
		.axis     = { 0, 0.4f, 0.9f },
	};
}

Clouds defaultClouds() {
	return Clouds {
		.present   = false,
		.height    = 1500,
		.thickness = 1200,
		.cover     = 0.45f,
		.density   = 0.03f,
		.span      = 3000,
	};
}

SceneFile emptySceneFile() {
	const Scene scene = {
		.programs       = {},
		.arguments      = {},
		.leaves         = {},
		.nodes          = {},
		.objects        = {},
		.objectPrograms = {},
	};

	const Looks looks = {
		.materials = {},
		.coats     = {},
		.groups    = {},
	};

	const Lighting lighting = {
		.lights = {},
		.sky    = defaultSky(),
		.clouds = defaultClouds(),
		.stars  = defaultStars(),
	};

	const Clip still = {
		.duration = 0,
		.rate     = 24,
		.cuts     = {},
	};

	const Shot shot = {
		.camera = defaultCamera(),
		.render = defaultRender(),
		.grade  = defaultGrade(),
		.clip   = still,
	};

	return SceneFile {
		.scene    = scene,
		.looks    = looks,
		.lighting = lighting,
		.shot     = shot,
		.timed    = false,
	};
}

bool hasSun(const Lighting& lighting) {
	for (const Light& light : lighting.lights) {
		if (light.kind == LightKind::Sun) {
			return true;
		}
	}
	return false;
}

std::string sceneError(const SceneFile& file, const std::string& path) {
	if (file.scene.objects.empty()) {
		return path + ": the scene has no shapes";
	}
	if (file.lighting.sky.kind == SkyKind::Physical && !hasSun(file.lighting)) {
		return path + ": a physical sky is lit by a sun light";
	}
	return {};
}

LoadedScene failedLoad(const std::string& error) {
	return LoadedScene {
		.file  = emptySceneFile(),
		.error = error,
	};
}

Scope timedScope(double time) {
	Scope scope;
	scope["time"] = numberValue(time);
	return scope;
}

LoadedScene loadScene(const std::string& path, double time) {
	takenFailure();
	SceneFile file = emptySceneFile();
	Scene& scene   = file.scene;

	Evaluator evaluator = {
		.scene           = scene,
		.looks           = file.looks,
		.lighting        = file.lighting,
		.shot            = file.shot,
		.files           = {},
		.trees           = {},
		.imported        = {},
		.definitions     = {},
		.scopes          = { timedScope(time) },
		.base            = 1,
		.compiler        = nullptr,
		.specialised     = {},
		.defaultMaterial = -1,
		.timed           = false,
	};

	const Location start = {
		.file   = nullptr,
		.line   = 1,
		.column = 1,
	};

	runFile(evaluator, path, start);
	if (failed()) {
		return failedLoad(takenFailure());
	}
	const std::string error = sceneError(file, path);
	if (!error.empty()) {
		return failedLoad(error);
	}
	scene.objects        = unionParts(scene);
	scene.objectPrograms = objectPrograms(scene);
	file.timed           = evaluator.timed;

	return LoadedScene {
		.file  = std::move(file),
		.error = {},
	};
}

}
