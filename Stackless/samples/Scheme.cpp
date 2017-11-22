#include "stdafx.h"

#include "Stackless.hpp"

#include <forward_list>
#include <iostream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace stackless;
using namespace stackless::microthreading;
using namespace stackless::timekeeping;

// return given mumber as a string
std::string str(long n) { std::ostringstream os; os << n; return os.str(); }

// return true iff given character is '0'..'9'
bool isdig(char c) { return isdigit(static_cast<unsigned char>(c)) != 0; }

////////////////////// cell

enum cell_type { Symbol, Number, List, Proc, Lambda };

struct environment; // forward declaration; cell and environment reference each other
typedef std::shared_ptr<typename environment> env_p;

					// a variant that can hold any kind of lisp value
struct cell {
	typedef cell(*proc_type)(const std::vector<cell> &);
	typedef std::vector<cell>::const_iterator iter;
	typedef std::map<std::string, cell> map;
	cell_type type; std::string val; std::vector<cell> list; proc_type proc; env_p env;
	cell(cell_type type = Symbol) : type(type), env(nullptr) {}
	cell(cell_type type, const std::string & val) : type(type), val(val), env(nullptr) {}
	cell(proc_type proc) : type(Proc), proc(proc), env(nullptr) {}
};

typedef std::vector<cell> cells;
typedef cells::const_iterator cellit;

const cell false_sym(Symbol, "#f");
const cell true_sym(Symbol, "#t"); // anything that isn't false_sym is true
const cell nil(Symbol, "nil");

namespace instruction {
	enum instruction {
		Quote,
		If,
		Set,
		Define,
		Lambda,
		Begin,
		Invalid
	};
}

struct SchemeInstructionConverter
	: public InstructionConverter<typename cell, typename instruction::instruction> {
	static _instruction_type convert(_cell_type value) {
		if (value.type != Symbol)
			return instruction::Invalid;
		else if (value.val == "quote") return instruction::Quote;
		else if (value.val == "if") return instruction::If;
		else if (value.val == "set!") return instruction::Set;
		else if (value.val == "define") return instruction::Define;
		else if (value.val == "lambda") return instruction::Lambda;
		else if (value.val == "begin") return instruction::Begin;
		return instruction::Invalid;
	}
};

////////////////////// environment

// a dictionary that (a) associates symbols with cells, and
// (b) can chain to an "outer" dictionary
struct environment : public Environment<cells> {
	typedef typename env_p env_p;

	environment(env_p outer = nullptr) : outer_(outer) {}

	environment(const cells & parms, const cells & args, env_p outer)
		: outer_(outer)
	{
		cellit a = args.begin();
		for (cellit p = parms.begin(); p != parms.end(); ++p)
			env_[p->val] = *a++;
	}

	// map a variable name onto a cell
	typedef std::map<std::string, cell> map;

	// return a reference to the innermost environment where 'var' appears
	map & find(const std::string & var)
	{
		if (env_.find(var) != env_.end())
			return env_; // the symbol exists in this environment
		if (outer_)
			return outer_->find(var); // attempt to find the symbol in some "outer" env
		std::cout << "unbound symbol '" << var << "'\n";
		exit(1);
	}

	// return a reference to the cell associated with the given symbol 'var'
	cell & operator[] (const std::string & var)
	{
		return env_[var];
	}

private:
	map env_; // inner symbol->cell mapping
	env_p outer_; // next adjacent outer env, or 0 if there are no further environments
};

// frame implementation
struct SchemeFrame : public Frame<typename cell, typename std::string, typename environment> {
	SchemeFrame(const cells &exps, env_p environment)
		: Frame(environment),
		exp(Symbol, "nil"),
		expressions(exps),
		arguments(),
		resolved_arguments(),
		exp_it(expressions.cbegin()),
		arg_it(arguments.cbegin()),
		resolved(false),
		subframe(nullptr)
	{
		if(!expressions.empty())
			setExpression(*exp_it);
	}

	bool isResolved() const { return resolved; }
	bool isArgumentsResolved() const { return true; }

	/**
	 * execute()
	 *
	 * Logic:
	 *  A) have subframe?
	 *    A=t      B) execute subframe
	 *      .      C) subframe resolved?
	 *      .  C=t D)   push frame result
	 *      .    . E)   delete subframe
	 *      .    . F)   goto Y
	 *      .  C=f G) goto Z
	 *    A=f      H) arg_it == arguments.end()
	 *      .  H=t I)   dispatch()
	 *      .    . J)   goto Z
	 *  Y) nextArgument()
	 *  Z) done.
	 */
	void execute() {
		if (subframe != nullptr) {
			subframe->execute();
			if (subframe->isResolved()) {
				resolved_arguments.push_back(subframe->result);
				delete subframe;
				nextArgument();
			}
		else if (arg_it == arguments.cend())
			dispatch();
		} else
			nextArgument();
	}


	void nextArgument() {
		++arg_it;
		if (arg_it == arguments.cend())
			dispatch();
		else if (resolveArgument(*arg_it)) {
			nextArgument();
		}
	}

	void nextExpression() {
		++exp_it;
		if (exp_it != expressions.cend()) {
			setExpression(*exp_it);
			resolved = false;
		} else {
			resolved = true;
			exp = nil;
		}
	}

	void setExpression(const cell &value) {
		resolved = false;
		arguments.clear();
		resolved_arguments.clear();
		if (resolveExpression(value)) {
			resolved = true;
			return;
		}
		nextArgument();
	}

	bool resolveArgument(const cell &value) {
		switch (value.type) {
		case Symbol:
			resolved_arguments.push_back(lookup(value.val));
			return true;
		case List:
			if (value.list.empty()) {
				resolved_arguments.push_back(value);
				return true;
			}
			if (value.list[0].type != Symbol) {
				// Not a function call
				resolved_arguments.push_back(value);
				return true;
			}
		default:
			resolved_arguments.push_back(value);
			return true;
		}
		// Function call. Create frame to execute it.
		env_p envptr = env;
		const cell &first = value.list[0];
		if (first.type == Lambda) {
			// parent env to lambda
			envptr = env_p(new environment(first.env));
		}
		subframe = new SchemeFrame(value.list, envptr);
		return false;
	}

	cell &lookup(const std::string &symbol) {
		return env->find(symbol)[symbol];
	}

	void dispatch();

	bool resolveExpression(const cell &value) {
		switch (value.type) {
		case Symbol:
			result = lookup(value.val);
			return true;
		case Number:
			result = value;
			return true;
		case List:
			if (value.list.empty()) {
				result = value;
				return true;
			}
			const auto &first = value.list[0];
			// iterator skips first item
			cells::const_iterator it = value.list.cbegin() + 1;
			arguments = cells();
			exp = value.list[0];
			if (exp.type == Symbol) {
				if (first.val == "quote") {         // (quote exp)
					result = value.list[1];
					return true;
				} else if (first.val == "if") {     // (if test conseq [alt])
					// becomes (if conseq alt test)
					// test
					arguments.push_back(*it); ++it;
					// conseq
					resolved_arguments.push_back(*it); ++it;
					// [alt]
					if (it != value.list.cend())
						resolved_arguments.push_back(value.list[2]);
					else
						resolved_arguments.push_back(nil);
					return false;
				} else if (first.val == "set!") {   // (set! var exp)
					// var
					resolved_arguments.push_back(*it); ++it;
					arguments.push_back(*it); ++it;
					return false;
				} else if (first.val == "define") { // (define var exp)
					// var
					resolved_arguments.push_back(*it); ++it;
					// exp
					arguments.push_back(*it); ++it;
					return false;
				} else if (first.val == "lambda") { // (lambda (var*) exp)
					result = value;
					result.type = cell_type::Lambda;
					result.env = env;
					return true;
				} else if (first.val == "begin") {  // (begin exp*)
					arguments = cells(value.list.cbegin() + 1, value.list.cend());
					return true;
				}
				// (proc exp*)
				arguments = cells(value.list.cbegin() + 1, value.list.cend());
				return false;
			}
			// Some other sort of list, unknown.
			resolved_arguments.push_back(value);
			return true;
		}
		throw std::runtime_error("Unhandled value type");
	}

	cell exp;
private:
	cells expressions;
	cells arguments;
	cells resolved_arguments;
	typename cells::const_iterator exp_it;
	typename cells::const_iterator arg_it;
	bool resolved;
	SchemeFrame *subframe;
};



template<typename instruction::instruction Instruction> struct SchemeDispatcher {
	static void dispatch(SchemeFrame &frame, cells::const_iterator args) {
		throw stackless::InvalidOperation<typename instruction::instruction, int, int>(Instruction, 0, 0);
	}
};

template<> struct SchemeDispatcher<instruction::If> {
	static void dispatch(SchemeFrame &frame, cells::const_iterator it) {
		const cell &conseq = *it; ++it;
		const cell &alt = *it; ++it;
		const cell &test = *it; ++it;
		const cell &if_result = (test.val == "#t") ? conseq : alt;
		switch (if_result.type) {
		case Symbol:
			frame.result = frame.env->find(if_result.val)[if_result.val];
			break;
		case List:
			if (if_result.list.empty()) {
				frame.result = if_result;
			} else {
				// Update our expression without moving exp it
				frame.exp = if_result;
				frame.setExpression(frame.exp);
			}
			break;
		default:
			frame.result = if_result;
			break;
		}
	}
};

void SchemeFrame::dispatch() {
	// all of our arguments are now resolved, dispatch
	cells::const_iterator it = resolved_arguments.cbegin();
	instruction::instruction ins = SchemeInstructionConverter::convert(exp);
	switch (ins) {
	case instruction::If:
		return SchemeDispatcher<instruction::If>::dispatch(*this, it);
	default:
		return SchemeDispatcher<instruction::Invalid>::dispatch(*this, it);
	}
}

////////////////////// built-in primitive procedures

cell proc_add(const cells & c)
{
	long n(atol(c[0].val.c_str()));
	for (cellit i = c.begin() + 1; i != c.end(); ++i) n += atol(i->val.c_str());
	return cell(Number, str(n));
}

cell proc_sub(const cells & c)
{
	long n(atol(c[0].val.c_str()));
	for (cellit i = c.begin() + 1; i != c.end(); ++i) n -= atol(i->val.c_str());
	return cell(Number, str(n));
}

cell proc_mul(const cells & c)
{
	long n(1);
	for (cellit i = c.begin(); i != c.end(); ++i) n *= atol(i->val.c_str());
	return cell(Number, str(n));
}

cell proc_div(const cells & c)
{
	long n(atol(c[0].val.c_str()));
	for (cellit i = c.begin() + 1; i != c.end(); ++i) n /= atol(i->val.c_str());
	return cell(Number, str(n));
}

cell proc_greater(const cells & c)
{
	long n(atol(c[0].val.c_str()));
	for (cellit i = c.begin() + 1; i != c.end(); ++i)
		if (n <= atol(i->val.c_str()))
			return false_sym;
	return true_sym;
}

cell proc_less(const cells & c)
{
	long n(atol(c[0].val.c_str()));
	for (cellit i = c.begin() + 1; i != c.end(); ++i)
		if (n >= atol(i->val.c_str()))
			return false_sym;
	return true_sym;
}

cell proc_less_equal(const cells & c)
{
	long n(atol(c[0].val.c_str()));
	for (cellit i = c.begin() + 1; i != c.end(); ++i)
		if (n > atol(i->val.c_str()))
			return false_sym;
	return true_sym;
}

cell proc_length(const cells & c) { return cell(Number, str((long)c[0].list.size())); }
cell proc_nullp(const cells & c) { return c[0].list.empty() ? true_sym : false_sym; }
cell proc_head(const cells & c) { return c[0].list[0]; }

cell proc_tail(const cells & c)
{
	if (c[0].list.size() < 2)
		return nil;
	cell result(c[0]);
	result.list.erase(result.list.begin());
	return result;
}

cell proc_append(const cells & c)
{
	cell result(List);
	result.list = c[0].list;
	for (cellit i = c[1].list.begin(); i != c[1].list.end(); ++i) result.list.push_back(*i);
	return result;
}

cell proc_cons(const cells & c)
{
	cell result(List);
	result.list.push_back(c[0]);
	for (cellit i = c[1].list.begin(); i != c[1].list.end(); ++i) result.list.push_back(*i);
	return result;
}

cell proc_list(const cells & c)
{
	cell result(List); result.list = c;
	return result;
}

// define the bare minimum set of primintives necessary to pass the unit tests
void add_globals(environment & env)
{
	env["nil"] = nil;   env["#f"] = false_sym;  env["#t"] = true_sym;
	env["append"] = cell(&proc_append);   env["head"] = cell(&proc_head);
	env["tail"] = cell(&proc_tail);      env["cons"] = cell(&proc_cons);
	env["length"] = cell(&proc_length);   env["list"] = cell(&proc_list);
	env["null?"] = cell(&proc_nullp);    env["+"] = cell(&proc_add);
	env["-"] = cell(&proc_sub);      env["*"] = cell(&proc_mul);
	env["/"] = cell(&proc_div);      env[">"] = cell(&proc_greater);
	env["<"] = cell(&proc_less);     env["<="] = cell(&proc_less_equal);
}
