#include "syntax/parser.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kodanuki {

thread_local std::string firstFailure;

void fail(const Location& location, const std::string& message) {
	if (!firstFailure.empty()) {
		return;
	}
	const std::string file = location.file ? *location.file : "?";
	firstFailure           = file + ":" + std::to_string(location.line) + ":" + std::to_string(location.column) + ": " + message;
}

bool failed() {
	return !firstFailure.empty();
}

std::string takenFailure() {
	const std::string taken = firstFailure;
	firstFailure.clear();
	return taken;
}

enum class TokenKind : std::uint8_t {
	Number,
	Name,
	String,
	Colour,
	Symbol,
	End,
};

struct Token {
	TokenKind kind;
	std::string text;
	double number;
	Location location;
};

struct Lexer {
	const std::string& text;
	std::size_t at;
	Location location;
};

char peekChar(const Lexer& lexer, std::size_t ahead) {
	const std::size_t index = lexer.at + ahead;
	return index < lexer.text.size() ? lexer.text[index] : '\0';
}

void advance(Lexer& lexer) {
	if (peekChar(lexer, 0) == '\n') {
		lexer.location.line += 1;
		lexer.location.column = 0;
	}
	lexer.location.column += 1;
	lexer.at += 1;
}

void advanceBy(Lexer& lexer, int count) {
	for (int i = 0; i < count; ++i) {
		advance(lexer);
	}
}

bool startsComment(const Lexer& lexer, char second) {
	return peekChar(lexer, 0) == '/' && peekChar(lexer, 1) == second;
}

void skipLineComment(Lexer& lexer) {
	while (peekChar(lexer, 0) != '\n' && peekChar(lexer, 0) != '\0') {
		advance(lexer);
	}
}

void skipBlockComment(Lexer& lexer) {
	const Location start = lexer.location;
	advanceBy(lexer, 2);
	while (!(peekChar(lexer, 0) == '*' && peekChar(lexer, 1) == '/')) {
		if (peekChar(lexer, 0) == '\0') {
			fail(start, "comment never ends");
			return;
		}
		advance(lexer);
	}
	advanceBy(lexer, 2);
}

bool isSpace(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void skipSpace(Lexer& lexer) {
	while (!failed()) {
		if (startsComment(lexer, '/')) {
			skipLineComment(lexer);
			continue;
		}
		if (startsComment(lexer, '*')) {
			skipBlockComment(lexer);
			continue;
		}
		if (!isSpace(peekChar(lexer, 0))) {
			return;
		}
		advance(lexer);
	}
}

bool isDigit(char c) {
	return c >= '0' && c <= '9';
}

bool isNameChar(char c) {
	return isDigit(c) || c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isHex(char c) {
	return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::string takeWhile(Lexer& lexer, bool (*keep)(char)) {
	const std::size_t start = lexer.at;
	while (keep(peekChar(lexer, 0))) {
		advance(lexer);
	}
	return lexer.text.substr(start, lexer.at - start);
}

void takeFraction(Lexer& lexer) {
	if (peekChar(lexer, 0) == '.' && isDigit(peekChar(lexer, 1))) {
		advance(lexer);
		takeWhile(lexer, isDigit);
	}
}

void takeExponent(Lexer& lexer) {
	const char e            = peekChar(lexer, 0);
	const char sign         = peekChar(lexer, 1);
	const bool signedDigits = (sign == '+' || sign == '-') && isDigit(peekChar(lexer, 2));
	if ((e == 'e' || e == 'E') && (isDigit(sign) || signedDigits)) {
		advanceBy(lexer, 2);
		takeWhile(lexer, isDigit);
	}
}

Token lexNumber(Lexer& lexer) {
	const Location location = lexer.location;
	const std::size_t start = lexer.at;
	takeWhile(lexer, isDigit);
	takeFraction(lexer);
	takeExponent(lexer);
	const std::string text = lexer.text.substr(start, lexer.at - start);
	return Token {
		.kind     = TokenKind::Number,
		.text     = text,
		.number   = std::stod(text),
		.location = location,
	};
}

Token lexColour(Lexer& lexer) {
	const Location location = lexer.location;
	advance(lexer);
	const std::string digits = takeWhile(lexer, isHex);
	if (digits.size() != 6) {
		fail(location, "a colour is # and six hex digits");
	}
	return Token {
		.kind     = TokenKind::Colour,
		.text     = digits,
		.number   = 0,
		.location = location,
	};
}

Token lexString(Lexer& lexer) {
	const Location location = lexer.location;
	advance(lexer);
	const std::size_t start = lexer.at;
	while (peekChar(lexer, 0) != '"') {
		if (peekChar(lexer, 0) == '\0' || peekChar(lexer, 0) == '\n') {
			fail(location, "string never ends");
			break;
		}
		advance(lexer);
	}
	const std::string text = lexer.text.substr(start, lexer.at - start);
	advance(lexer);
	return Token {
		.kind     = TokenKind::String,
		.text     = text,
		.number   = 0,
		.location = location,
	};
}

std::optional<Token> lexPair(Lexer& lexer) {
	const Location location = lexer.location;
	const std::string pair  = { peekChar(lexer, 0), peekChar(lexer, 1) };
	for (const char* two : { "..", "==", "!=", "<=", ">=", "&&", "||" }) {
		if (pair == two) {
			advanceBy(lexer, 2);
			return Token {
				.kind     = TokenKind::Symbol,
				.text     = pair,
				.number   = 0,
				.location = location,
			};
		}
	}
	return std::nullopt;
}

Token lexSymbol(Lexer& lexer) {
	if (std::optional<Token> pair = lexPair(lexer)) {
		return *pair;
	}
	const Location location = lexer.location;
	const char c            = peekChar(lexer, 0);
	if (std::string("(){}[],;=+-*/<>!.").find(c) == std::string::npos) {
		fail(location, std::string("unexpected character '") + c + "'");
	}
	advance(lexer);
	return Token {
		.kind     = TokenKind::Symbol,
		.text     = std::string(1, c),
		.number   = 0,
		.location = location,
	};
}

Token lexName(Lexer& lexer) {
	const Location location = lexer.location;
	return Token {
		.kind     = TokenKind::Name,
		.text     = takeWhile(lexer, isNameChar),
		.number   = 0,
		.location = location,
	};
}

Token endToken(const Lexer& lexer) {
	return Token {
		.kind     = TokenKind::End,
		.text     = {},
		.number   = 0,
		.location = lexer.location,
	};
}

Token lexToken(Lexer& lexer) {
	const char c = peekChar(lexer, 0);
	if (c == '\0') {
		return endToken(lexer);
	}
	if (isDigit(c)) {
		return lexNumber(lexer);
	}
	if (isNameChar(c)) {
		return lexName(lexer);
	}
	if (c == '#') {
		return lexColour(lexer);
	}
	if (c == '"') {
		return lexString(lexer);
	}
	return lexSymbol(lexer);
}

std::vector<Token> tokenize(const std::string& text, const std::string* file) {
	Lexer lexer = {
		.text     = text,
		.at       = 0,
		.location = Location {
			.file   = file,
			.line   = 1,
			.column = 1,
		},
	};

	std::vector<Token> tokens;
	while (true) {
		skipSpace(lexer);
		tokens.push_back(failed() ? endToken(lexer) : lexToken(lexer));
		if (tokens.back().kind == TokenKind::End) {
			return tokens;
		}
	}
}

struct Parser {
	const std::vector<Token>& tokens;
	std::size_t at;
};

const Token& peekToken(const Parser& parser, std::size_t ahead) {
	const std::size_t last  = parser.tokens.size() - 1;
	const std::size_t index = failed() ? last : std::min(parser.at + ahead, last);
	return parser.tokens[index];
}

const Token& takeToken(Parser& parser) {
	const Token& token = peekToken(parser, 0);
	parser.at += 1;
	return token;
}

bool isSymbol(const Token& token, const char* text) {
	return token.kind == TokenKind::Symbol && token.text == text;
}

bool isName(const Token& token, const char* text) {
	return token.kind == TokenKind::Name && token.text == text;
}

bool accept(Parser& parser, const char* symbol) {
	if (!isSymbol(peekToken(parser, 0), symbol)) {
		return false;
	}
	parser.at += 1;
	return true;
}

void expect(Parser& parser, const char* symbol) {
	if (!accept(parser, symbol)) {
		fail(peekToken(parser, 0).location, std::string("expected '") + symbol + "'");
	}
}

void expectWord(Parser& parser, const char* word) {
	if (!isName(peekToken(parser, 0), word)) {
		fail(peekToken(parser, 0).location, std::string("expected '") + word + "'");
	}
	parser.at += 1;
}

std::string expectName(Parser& parser) {
	const Token& token = peekToken(parser, 0);
	if (token.kind != TokenKind::Name) {
		fail(token.location, "expected a name");
	}
	parser.at += 1;
	return token.text;
}

bool continues(Parser& parser, const char* close) {
	if (accept(parser, ",")) {
		return !accept(parser, close);
	}
	expect(parser, close);
	return false;
}

Expression parseExpression(Parser& parser);

Expression makeExpression(ExpressionKind kind, const Location& location, const std::string& text) {
	return Expression {
		.kind     = kind,
		.location = location,
		.text     = text,
		.number   = 0,
		.items    = {},
		.names    = {},
	};
}

bool isNamedArgument(const Parser& parser) {
	return peekToken(parser, 0).kind == TokenKind::Name && isSymbol(peekToken(parser, 1), "=");
}

void parseArgument(Parser& parser, Expression& call) {
	if (!isNamedArgument(parser)) {
		call.names.push_back("");
		call.items.push_back(parseExpression(parser));
		return;
	}
	call.names.push_back(expectName(parser));
	expect(parser, "=");
	call.items.push_back(parseExpression(parser));
}

void parseArguments(Parser& parser, Expression& call) {
	if (accept(parser, ")")) {
		return;
	}
	do {
		parseArgument(parser, call);
	} while (continues(parser, ")"));
}

Expression parseList(Parser& parser, const Location& location) {
	Expression list = makeExpression(ExpressionKind::List, location, "");
	if (accept(parser, "]")) {
		return list;
	}
	do {
		list.items.push_back(parseExpression(parser));
	} while (continues(parser, "]"));
	return list;
}

Expression parseGroup(Parser& parser) {
	const Expression inner = parseExpression(parser);
	expect(parser, ")");
	return inner;
}

Expression parsePrimary(Parser& parser) {
	const Token& token = takeToken(parser);
	switch (token.kind) {
		case TokenKind::Number: return Expression {
			.kind     = ExpressionKind::Number,
			.location = token.location,
			.text     = {},
			.number   = token.number,
			.items    = {},
			.names    = {},
		};
		case TokenKind::Colour: return makeExpression(ExpressionKind::Colour, token.location, token.text);
		case TokenKind::Name: return makeExpression(ExpressionKind::Name, token.location, token.text);
		default: break;
	}

	if (isSymbol(token, "(")) {
		return parseGroup(parser);
	}
	if (isSymbol(token, "[")) {
		return parseList(parser, token.location);
	}
	fail(token.location, "expected a value");
	return makeExpression(ExpressionKind::Number, token.location, "");
}

Expression parseIndex(Parser& parser, const Expression& target) {
	Expression index = makeExpression(ExpressionKind::Index, target.location, "");
	index.items      = { target, parseExpression(parser) };
	expect(parser, "]");
	return index;
}

Expression parseMember(Parser& parser, const Expression& target) {
	Expression member = makeExpression(ExpressionKind::Member, target.location, expectName(parser));
	member.items      = { target };
	return member;
}

Expression parsePostfix(Parser& parser) {
	Expression expression = parsePrimary(parser);
	while (true) {
		if (expression.kind == ExpressionKind::Name && accept(parser, "(")) {
			expression.kind = ExpressionKind::Call;
			parseArguments(parser, expression);
			continue;
		}
		if (accept(parser, "[")) {
			expression = parseIndex(parser, expression);
			continue;
		}
		if (accept(parser, ".")) {
			expression = parseMember(parser, expression);
			continue;
		}
		return expression;
	}
}

Expression parseUnary(Parser& parser) {
	const Token& token = peekToken(parser, 0);
	if (!isSymbol(token, "-") && !isSymbol(token, "!")) {
		return parsePostfix(parser);
	}
	parser.at += 1;
	Expression unary = makeExpression(ExpressionKind::Unary, token.location, token.text);
	unary.items      = { parseUnary(parser) };
	return unary;
}

const std::vector<std::vector<std::string>>& binaryLevels() {
	static const std::vector<std::vector<std::string>> levels = {
		{ "||" },
		{ "&&" },
		{ "==", "!=", "<", "<=", ">", ">=" },
		{ "+", "-" },
		{ "*", "/" },
	};

	return levels;
}

bool isLevelOperator(const Token& token, std::size_t level) {
	const std::vector<std::string>& symbols = binaryLevels()[level];
	return token.kind == TokenKind::Symbol && std::find(symbols.begin(), symbols.end(), token.text) != symbols.end();
}

Expression parseLevel(Parser& parser, std::size_t level) {
	if (level == binaryLevels().size()) {
		return parseUnary(parser);
	}
	Expression left = parseLevel(parser, level + 1);
	while (isLevelOperator(peekToken(parser, 0), level)) {
		const Token& token = takeToken(parser);
		Expression binary  = makeExpression(ExpressionKind::Binary, token.location, token.text);
		binary.items       = { left, parseLevel(parser, level + 1) };
		left               = binary;
	}
	return left;
}

Expression parseExpression(Parser& parser) {
	return parseLevel(parser, 0);
}

Statement makeStatement(StatementKind kind, const Location& location, const std::string& text) {
	return Statement {
		.kind        = kind,
		.location    = location,
		.text        = text,
		.word        = {},
		.names       = {},
		.expressions = {},
		.body        = {},
	};
}

std::vector<Statement> parseBlock(Parser& parser);
Statement parseStatement(Parser& parser);

Statement parseImport(Parser& parser, const Location& location) {
	const Token& path = takeToken(parser);
	if (path.kind != TokenKind::String) {
		fail(path.location, "expected a path in quotes");
	}
	expect(parser, ";");
	return makeStatement(StatementKind::Import, location, path.text);
}

Statement parseLet(Parser& parser, const Location& location) {
	Statement let = makeStatement(StatementKind::Let, location, expectName(parser));
	expect(parser, "=");
	let.expressions = { parseExpression(parser) };
	expect(parser, ";");
	return let;
}

Expression parseLoopSource(Parser& parser) {
	const Expression from   = parseExpression(parser);
	const Location location = peekToken(parser, 0).location;
	if (!accept(parser, "..")) {
		return from;
	}
	Expression range = makeExpression(ExpressionKind::Binary, location, "..");
	range.items      = { from, parseExpression(parser) };
	return range;
}

Statement parseFor(Parser& parser, const Location& location) {
	Statement loop = makeStatement(StatementKind::For, location, "");
	do {
		loop.names.push_back(expectName(parser));
		expectWord(parser, "in");
		loop.expressions.push_back(parseLoopSource(parser));
	} while (accept(parser, ","));
	loop.body = parseBlock(parser);
	return loop;
}

Statement parseIf(Parser& parser, const Location& location) {
	Statement branch   = makeStatement(StatementKind::If, location, "");
	branch.expressions = { parseExpression(parser) };
	branch.body        = parseBlock(parser);
	return branch;
}

Statement parseReturn(Parser& parser, const Location& location) {
	Statement result   = makeStatement(StatementKind::Return, location, "");
	result.expressions = { parseExpression(parser) };
	expect(parser, ";");
	return result;
}

std::vector<std::string> parseParameters(Parser& parser) {
	std::vector<std::string> names;
	expect(parser, "(");
	if (accept(parser, ")")) {
		return names;
	}
	do {
		names.push_back(expectName(parser));
	} while (continues(parser, ")"));
	return names;
}

Statement parseDefinition(Parser& parser, StatementKind kind, const Location& location) {
	Statement definition = makeStatement(kind, location, expectName(parser));
	definition.names     = parseParameters(parser);
	definition.body      = parseBlock(parser);
	return definition;
}

void parseField(Parser& parser, Statement& setting) {
	setting.names.push_back(expectName(parser));
	expect(parser, "=");
	setting.expressions.push_back(parseExpression(parser));
	expect(parser, ";");
}

Statement parseSetting(Parser& parser, const Location& location, const std::string& block) {
	Statement setting = makeStatement(StatementKind::Setting, location, block);
	if (block == "sky" || block == "light") {
		setting.word = expectName(parser);
	}
	expect(parser, "{");
	while (!failed() && !accept(parser, "}")) {
		parseField(parser, setting);
	}
	return setting;
}

bool isOneOf(const Token& token, std::initializer_list<const char*> names) {
	for (const char* name : names) {
		if (isName(token, name)) {
			return true;
		}
	}
	return false;
}

bool isModifier(const Parser& parser) {
	return isOneOf(peekToken(parser, 0), { "at", "turn", "scale", "paint", "coat", "rule", "group" }) && isSymbol(peekToken(parser, 1), "(");
}

bool isSetting(const Parser& parser) {
	const Token& token = peekToken(parser, 0);
	if (isOneOf(token, { "render", "camera", "grade", "clouds", "stars" })) {
		return isSymbol(peekToken(parser, 1), "{");
	}
	if (isName(token, "clip")) {
		return isSymbol(peekToken(parser, 1), "{") && peekToken(parser, 2).kind == TokenKind::Name && isSymbol(peekToken(parser, 3), "=");
	}
	return isOneOf(token, { "sky", "light" }) && isSymbol(peekToken(parser, 2), "{");
}

std::vector<Expression> parseModifiers(Parser& parser) {
	std::vector<Expression> modifiers;
	while (isModifier(parser)) {
		modifiers.push_back(parsePostfix(parser));
	}
	return modifiers;
}

Statement parseJoin(Parser& parser, const Location& location) {
	Statement join = makeStatement(StatementKind::Join, location, expectName(parser));
	if (accept(parser, "(")) {
		join.expressions = { parseGroup(parser) };
	}
	join.body = parseBlock(parser);
	return join;
}

Statement parseRepeat(Parser& parser, const Location& location) {
	Statement repeat   = makeStatement(StatementKind::Join, location, "repeat");
	repeat.expressions = { parsePostfix(parser) };
	if (repeat.expressions[0].kind != ExpressionKind::Call) {
		fail(location, "repeat takes count and step");
	}
	repeat.body = parseBlock(parser);
	return repeat;
}

Statement parsePlace(Parser& parser, const Location& location, bool block) {
	Statement place   = makeStatement(StatementKind::Place, location, "");
	place.expressions = parseModifiers(parser);
	if (place.expressions.empty()) {
		fail(location, "expected at, turn, scale, paint or coat");
	}
	if (block) {
		place.body = parseBlock(parser);
		return place;
	}
	if (isSymbol(peekToken(parser, 0), "{")) {
		fail(peekToken(parser, 0).location, "a block after modifiers needs 'with'");
	}
	place.body = { parseStatement(parser) };
	return place;
}

Statement parseCall(Parser& parser, const Location& location) {
	Statement call   = makeStatement(StatementKind::Call, location, "");
	call.expressions = { parseExpression(parser) };
	if (call.expressions[0].kind != ExpressionKind::Call) {
		fail(location, "a statement must call something");
	}
	expect(parser, ";");
	return call;
}

std::optional<Statement> parseKeyword(Parser& parser) {
	const Token& token                          = peekToken(parser, 0);
	const Location location                     = token.location;
	const std::string word                      = token.kind == TokenKind::Name ? token.text : "";
	static const std::set<std::string> keywords = { "import", "let", "for", "if", "return", "function", "shape", "with" };
	if (!keywords.contains(word)) {
		return std::nullopt;
	}
	parser.at += 1;
	if (word == "import") {
		return parseImport(parser, location);
	}
	if (word == "let") {
		return parseLet(parser, location);
	}
	if (word == "for") {
		return parseFor(parser, location);
	}
	if (word == "if") {
		return parseIf(parser, location);
	}
	if (word == "return") {
		return parseReturn(parser, location);
	}
	if (word == "with") {
		return parsePlace(parser, location, true);
	}
	return parseDefinition(parser, word == "function" ? StatementKind::Function : StatementKind::Shape, location);
}

Statement parseStatement(Parser& parser) {
	if (std::optional<Statement> keyword = parseKeyword(parser)) {
		return *keyword;
	}
	const Token& token = peekToken(parser, 0);
	if (isSetting(parser)) {
		parser.at += 1;
		return parseSetting(parser, token.location, token.text);
	}
	if (isModifier(parser)) {
		return parsePlace(parser, token.location, false);
	}
	if (isOneOf(token, { "merge", "cut", "clip" })) {
		return parseJoin(parser, token.location);
	}
	if (isOneOf(token, { "repeat" })) {
		return parseRepeat(parser, token.location);
	}
	return parseCall(parser, token.location);
}

std::vector<Statement> parseBlock(Parser& parser) {
	expect(parser, "{");
	std::vector<Statement> body;
	while (!accept(parser, "}")) {
		if (peekToken(parser, 0).kind == TokenKind::End) {
			fail(peekToken(parser, 0).location, "block never ends");
			return body;
		}
		body.push_back(parseStatement(parser));
	}
	return body;
}

std::vector<Statement> parseFile(const std::vector<Token>& tokens) {
	Parser parser = {
		.tokens = tokens,
		.at     = 0,
	};

	std::vector<Statement> statements;
	while (peekToken(parser, 0).kind != TokenKind::End) {
		statements.push_back(parseStatement(parser));
	}
	return statements;
}

std::vector<Statement> parseSource(const std::string& text, const std::string* file) {
	const std::vector<Token> tokens = tokenize(text, file);
	if (failed()) {
		return {};
	}
	return parseFile(tokens);
}

}
