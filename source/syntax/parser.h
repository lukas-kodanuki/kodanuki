#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kodanuki {

/**
 * What kind of expression a node of the syntax tree is.
 */
enum class ExpressionKind : std::uint8_t {
	/**
	 * A number written out.
	 */
	Number,
	/**
	 * A colour written as #RRGGBB, its text the six digits.
	 */
	Colour,
	/**
	 * A name, its text the name.
	 */
	Name,
	/**
	 * A list, its items the elements.
	 */
	List,
	/**
	 * A call, its text the name called and its items the arguments.
	 */
	Call,
	/**
	 * An index, its items the list and the index.
	 */
	Index,
	/**
	 * A member such as .x or .xz, its text the member and its item the
	 * list.
	 */
	Member,
	/**
	 * A unary operator, its text the operator and its item the operand.
	 */
	Unary,
	/**
	 * A binary operator, its text the operator and its items the
	 * operands.
	 */
	Binary,
};

/**
 * What kind of statement a node of the syntax tree is.
 */
enum class StatementKind : std::uint8_t {
	/**
	 * An import, its text the path.
	 */
	Import,
	/**
	 * A binding or a change of one, its text the name and its expression
	 * the value.
	 */
	Let,
	/**
	 * A loop, one name and one source expression per level, outermost
	 * first.
	 */
	For,
	/**
	 * A branch, its expression the condition.
	 */
	If,
	/**
	 * A return of a function, its expression the value.
	 */
	Return,
	/**
	 * A function's definition, its text the name and its names the
	 * parameters.
	 */
	Function,
	/**
	 * A shape's definition, its text the name and its names the
	 * parameters.
	 */
	Shape,
	/**
	 * A settings block, its text the block, its word the kind of a sky
	 * or a light, and one name and one expression per field.
	 */
	Setting,
	/**
	 * Modifiers before a statement or a `with` block, its expressions the
	 * modifier calls, outermost first.
	 */
	Place,
	/**
	 * A join (merge, cut or clip, its expression the radius where one is
	 * given) or a repeat (its expression the call with its count and
	 * step), its text the word and its body the block.
	 */
	Join,
	/**
	 * A call of a function or a shape that emits it, its expression the
	 * call.
	 */
	Call,
};

/**
 * Where something stands in a scene file.
 */
struct Location {
	/**
	 * The file's path, owned by the evaluator; null before any file.
	 */
	const std::string* file;
	/**
	 * The line, from 1.
	 */
	int line;
	/**
	 * The column, from 1.
	 */
	int column;
};

/**
 * An expression of the syntax tree.
 */
struct Expression {
	/**
	 * What kind it is.
	 */
	ExpressionKind kind;
	/**
	 * Where it stands.
	 */
	Location location;
	/**
	 * The name, the operator or the digits, as its kind says.
	 */
	std::string text;
	/**
	 * The number of a Number.
	 */
	double number;
	/**
	 * The operands, elements or arguments, as its kind says.
	 */
	std::vector<Expression> items;
	/**
	 * A call's argument names matching its items one for one, empty for
	 * a positional argument.
	 */
	std::vector<std::string> names;
};

/**
 * A statement of the syntax tree; each kind reads the fields it says.
 */
struct Statement {
	/**
	 * What kind it is.
	 */
	StatementKind kind;
	/**
	 * Where it stands.
	 */
	Location location;
	/**
	 * The name, path or word, as its kind says.
	 */
	std::string text;
	/**
	 * The kind of a sky or a light block.
	 */
	std::string word;
	/**
	 * The loop variables, parameters or field names, as its kind says.
	 */
	std::vector<std::string> names;
	/**
	 * The expressions, as its kind says.
	 */
	std::vector<Expression> expressions;
	/**
	 * The block the statement holds.
	 */
	std::vector<Statement> body;
};

/**
 * Parse a scene file's text into its statements.
 *
 * @param text The text.
 * @param file The file's path, for locations.
 */
std::vector<Statement> parseSource(const std::string& text, const std::string* file);

/**
 * Note an error of the load running on this thread as
 * `file:line:column: message`, unless that load has met one already.
 *
 * The first error is the load's result (rule 19): the caller returns at
 * once with a neutral value, the parser then sees only the end of the
 * text, and the evaluator runs no further statement or expression, so
 * the load ends quickly and loadScene returns the error as a value.
 *
 * @param location Where the error stands.
 * @param message What is wrong.
 */
void fail(const Location& location, const std::string& message);

/**
 * Whether the load running on this thread has met an error.
 */
bool failed();

/**
 * Return the first error the load running on this thread met, empty
 * when it met none, and forget it, so the thread's next load starts
 * clean.
 */
std::string takenFailure();

}
