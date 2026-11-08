#pragma once

#include "sdf/scene.h"
#include "syntax/parser.h"

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace kodanuki {

struct Pattern;

/**
 * What kind of thing a value of the scene language is.
 */
enum class ValueKind : std::uint8_t {
	/**
	 * A number, known when the scene loads or held in a register of the
	 * function being compiled.
	 */
	Number,
	/**
	 * A list of values of one kind; a list of numbers is a vector.
	 */
	List,
	/**
	 * A material, as an index into the scene's materials.
	 */
	Material,
	/**
	 * A call of a function, to be emitted as a shape or read as a
	 * pattern.
	 */
	Pattern,
	/**
	 * A bare name such as an axis or a setting's word.
	 */
	Word,
};

/**
 * A value of the scene language while a file runs.
 */
struct Value {
	/**
	 * What kind of value it is; only the fields of that kind are read.
	 */
	ValueKind kind;
	/**
	 * The number, where it is known when the scene loads.
	 */
	double number;
	/**
	 * The register of the function being compiled that holds the number,
	 * or -1 where the number is known.
	 */
	std::int32_t slot;
	/**
	 * The elements of a list.
	 */
	std::vector<Value> items;
	/**
	 * The material, as an index into the scene's materials.
	 */
	std::uint32_t material;
	/**
	 * The pattern.
	 */
	std::shared_ptr<const Pattern> pattern;
	/**
	 * The word.
	 */
	std::string word;
};

/**
 * A call of a function kept to be compiled later: as a shape's leaf, a
 * ramp's pattern or a coat's mask.
 */
struct Pattern {
	/**
	 * The function's definition.
	 */
	const Statement* function;
	/**
	 * The arguments it was called with.
	 */
	std::vector<Value> arguments;
	/**
	 * Where it was called, for errors.
	 */
	Location location;
	/**
	 * Whether it is read in world space rather than the painted shape's.
	 */
	bool world;
	/**
	 * The colours of a ramp over it, empty for a plain call.
	 */
	std::vector<Value> stops;
};

/**
 * The bindings of one block, by name.
 */
using Scope = std::unordered_map<std::string, Value>;

/**
 * A function being compiled into a program of steps.
 */
struct Compiler {
	/**
	 * The program so far.
	 */
	Program program;
	/**
	 * The step that holds each constant, so each is held once.
	 */
	std::map<double, std::uint32_t> constants;
	/**
	 * The point the function reads, as a list of three registers.
	 */
	Value point;
	/**
	 * The surface normal a pattern reads, as a list of three registers.
	 */
	Value normal;
	/**
	 * Whether the function has returned.
	 */
	bool returned;
	/**
	 * What it returned.
	 */
	Value result;
};

/**
 * What is around a statement that emits shapes: the placement, the
 * paint, the coats and where the shapes go.
 */
struct Context {
	/**
	 * The linear part of the placement from the shape's space to world
	 * space, by rows.
	 */
	double linear[3][3];
	/**
	 * Where the shape's origin lands in world space.
	 */
	double origin[3];
	/**
	 * What the shape's distance is multiplied by: the smallest scale,
	 * so the distance never grows past the true one.
	 */
	double stretch;
	/**
	 * The material, or -1 for the default grey.
	 */
	std::int32_t material;
	/**
	 * The coats, innermost first, as indices into the scene's coats.
	 */
	std::vector<std::uint32_t> coats;
	/**
	 * The rules the shapes say, Rule bits or'ed.
	 */
	std::uint8_t rules;
	/**
	 * The groups the shapes belong to, one bit per group.
	 */
	std::uint64_t groups;
	/**
	 * Where emitted nodes go: a join's children, or the scene's objects
	 * where it is null.
	 */
	std::vector<std::uint32_t>* sink;
};

/**
 * The state of a scene while its files run.
 */
struct Evaluator {
	/**
	 * The geometry being built.
	 */
	Scene& scene;
	/**
	 * The looks being built.
	 */
	Looks& looks;
	/**
	 * The lighting being built.
	 */
	Lighting& lighting;
	/**
	 * The shot being built.
	 */
	Shot& shot;
	/**
	 * The paths of the files read, which locations point into.
	 */
	std::list<std::string> files;
	/**
	 * The statements of every file read, which definitions point into.
	 */
	std::list<std::vector<Statement>> trees;
	/**
	 * The files already imported, which are not read again.
	 */
	std::set<std::string> imported;
	/**
	 * The functions and shapes, by name.
	 */
	std::unordered_map<std::string, const Statement*> definitions;
	/**
	 * The scopes, the file's top level first.
	 */
	std::vector<Scope> scopes;
	/**
	 * The first scope the running definition sees past the top level.
	 */
	std::size_t base;
	/**
	 * The function being compiled, or null while shapes are built.
	 */
	Compiler* compiler;
	/**
	 * Each function compiled for a distinct set of list arguments, as an
	 * index into the scene's programs, by a key of the function and the
	 * lists.
	 */
	std::map<std::string, std::uint32_t> specialised;
	/**
	 * The default grey material once it is made, or -1.
	 */
	std::int32_t defaultMaterial;
};

/**
 * One step of a built-in to fold when its operands are known, or to
 * compile otherwise.
 */
struct Operation {
	/**
	 * What the step does.
	 */
	Opcode opcode;
	/**
	 * The first operand.
	 */
	Value a;
	/**
	 * The second operand.
	 */
	Value b;
	/**
	 * The third operand.
	 */
	Value c;
	/**
	 * Where it was written, for errors.
	 */
	Location location;
};

/**
 * A call of a function or a shape to run.
 */
struct Invocation {
	/**
	 * The definition.
	 */
	const Statement& definition;
	/**
	 * The arguments, in order.
	 */
	const std::vector<Value>& arguments;
	/**
	 * The context its shapes are emitted in.
	 */
	const Context& context;
	/**
	 * Where it was called, for errors.
	 */
	Location location;
};

/**
 * A lattice of copies, as `repeat(count = ..., step = ...)` asks.
 */
struct RepeatCall {
	/**
	 * How many copies along each axis of the block's space, one or more
	 * each.
	 */
	int counts[3];
	/**
	 * How far one copy stands from the one before it along each axis of
	 * the block's space, in world space: the step along that axis turned
	 * and scaled by the placement around the block.
	 */
	double steps[3][3];
	/**
	 * Where the statement stands, for errors.
	 */
	Location location;
};

/**
 * A lattice to emit: the call, its block and the context around it.
 */
struct Repetition {
	/**
	 * The counts and the steps.
	 */
	const RepeatCall& call;
	/**
	 * The statements of the block.
	 */
	const std::vector<Statement>& block;
	/**
	 * The placement, paint and coats around the block, and where its
	 * shapes go.
	 */
	const Context& context;
};

/**
 * A scene file as loaded: the scene, or the error that stopped it.
 */
struct LoadedScene {
	/**
	 * The scene; empty where the load met an error.
	 */
	SceneFile file;
	/**
	 * The first error as `file:line:column: message`, empty where the
	 * scene loaded.
	 */
	std::string error;
};

/**
 * Run a scene file and everything it imports, and return its
 * geometry, looks, lighting and shot, or the first error.
 *
 * Every long hard union at the top of an object is then cut into
 * objects of its own (unionParts), and every object is compiled into
 * one program. The first error stops the load and comes back as
 * `file:line:column: message`, with an empty scene.
 *
 * @param path The file.
 */
LoadedScene loadScene(const std::string& path);

/**
 * Return a list of values.
 *
 * @param items The elements.
 */
Value listValue(std::vector<Value> items);

/**
 * Return a number known when the scene loads.
 *
 * @param number The number.
 */
Value numberValue(double number);

/**
 * Return the context of the top level of a file: no placement, the
 * default material, no coats, shapes going to the scene's objects.
 */
Context rootContext();

/**
 * Return a number held in a register of the function being compiled.
 *
 * @param slot The register.
 */
Value slotValue(std::uint32_t slot);

/**
 * Append a step to the program being compiled and return its register.
 *
 * @param compiler The function being compiled (rule 46).
 * @param step The step.
 */
std::uint32_t pushStep(Compiler& compiler, const Step& step);

/**
 * Return the register that holds a number in the function being
 * compiled, adding a constant step for a known number.
 *
 * @param evaluator The evaluator.
 * @param value The number.
 */
std::uint32_t slotOf(Evaluator& evaluator, const Value& value);

/**
 * Run a function's body inside the function being compiled and return
 * the value it returns. It fails where the body never returns.
 *
 * @param evaluator The evaluator.
 * @param call The function, its arguments and where it was called.
 */
Value inlineFunction(Evaluator& evaluator, const Invocation& call);

/**
 * Run a lattice's block once and emit its shapes once per copy.
 *
 * The block runs once, so everything it draws or changes happens once
 * and every copy is alike. Its nodes are then copied for each copy but
 * the first, x fastest, then y, then z: the same programs, arguments,
 * materials and coats, every leaf's placement moved by the copy's
 * steps. The copies stand side by side: inside a shape one hard merge,
 * at the top level separate objects.
 *
 * @param evaluator The evaluator.
 * @param repetition The call, the block and the context.
 */
void emitRepeated(Evaluator& evaluator, const Repetition& repetition);

}
