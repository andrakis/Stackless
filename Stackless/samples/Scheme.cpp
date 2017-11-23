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
	cell(const std::vector<cell> &cells) : type(List), list(cells) {}
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
		Proc,
		Invalid
	};
}

struct SchemeInstructionConverter
	: public InstructionConverter<typename cell, typename instruction::instruction> {
	static _instruction_type convert(_cell_type value) {
		switch (value.type) {
			// These two are handled by the Proc dispatcher
		case Lambda:
		case Proc:
			return instruction::Proc;
		case Symbol:
			if (value.val == "quote") return instruction::Quote;
			if (value.val == "if") return instruction::If;
			if (value.val == "set!") return instruction::Set;
			if (value.val == "define") return instruction::Define;
			if (value.val == "lambda") return instruction::Lambda;
			if (value.val == "begin") return instruction::Begin;
			break;
		}
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
	SchemeFrame(const cell &expression, env_p environment)
		: Frame(environment),
		exp(expression),
		expressions(),
		arguments(),
		resolved_arguments(),
		exp_it(expressions.cbegin()),
		arg_it(arguments.cbegin()),
		resolved(false),
		subframe(nullptr),
		subframe_mode(None)
	{
		expressions.push_back(exp);
		exp_it = expressions.cbegin();
		setExpression(exp);
	}

	enum SubframeMode {
		None,
		Argument,
		Procedure
	};

	bool isResolved() const { return resolved; }
	bool isArgumentsResolved() const { return true; }

	/**
	 * execute()
	 *
	 * Logic:
	 *  A) have subframe?
	 *    A=t      B) execute subframe
	 *      .      C) subframe resolved?
	 *      .  C=t D) subframe_mode
	 *      .  C=t D=Argument  E) resolved_arguments.push(frame.result)
	 *      .    .   .         F) nextArgument()
	 *      .    .   .         G) goto L
	 *      .  C=t D=Procedure H) result = frame.result
	 *      .    .   .         I) nextExpression()
	 *      .    .   .         J) goto L
	 *      .  C=t D=*         K) error()
	 *      .    . L) delete subframe
	 *      .  C=* K) goto Z
	 *    A=f      L) arg_it == arguments.end()
	 *      .  H=t M)   dispatch()
	 *      .    . N)   goto Z
	 *  Z) done.
	 */
	void execute() {
		if (subframe != nullptr) {
			subframe->execute();
			if (subframe->isResolved()) {
				switch (subframe_mode) {
				case Argument:
					resolved_arguments.push_back(subframe->result);
					nextArgument();
					break;
				case Procedure:
					result = subframe->result;
					nextExpression();
					break;
				default:
					throw std::runtime_error("Invalid subframe mode None");
				}
				delete subframe;
				subframe_mode = None;
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
		subframe = new SchemeFrame(value, envptr);
		subframe_mode = Argument;
		return false;
	}

	cell &lookup(const std::string &symbol) {
		return env->find(symbol)[symbol];
	}

	bool dispatchCall();
	void dispatch() {
		if (dispatchCall())
			nextExpression();
	}

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
						resolved_arguments.push_back(*it);
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
					result.type = Lambda;
					result.env = env;
					return true;
				} else if (first.val == "begin") {  // (begin exp*)
					resolved_arguments = cells(value.list.cbegin() + 1, value.list.cend());
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
	cells expressions;
	cells arguments;
	cells resolved_arguments;
	typename cells::const_iterator exp_it;
	typename cells::const_iterator arg_it;
	bool resolved;
	SchemeFrame *subframe;
	SubframeMode subframe_mode;
};



template<typename instruction::instruction Instruction> struct SchemeDispatcher {
	static bool dispatch(SchemeFrame &frame, cells::const_iterator args) {
		throw stackless::InvalidOperation<typename instruction::instruction, int, int>(Instruction, 0, 0);
	}
};

template<> struct SchemeDispatcher<instruction::If> {
	static bool dispatch(SchemeFrame &frame, cells::const_iterator it) {
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
				return false;
			}
			break;
		default:
			frame.result = if_result;
			break;
		}
		// Move exp_it
		return true;
	}
};

template<> struct SchemeDispatcher<instruction::Begin> {
	static bool dispatch(SchemeFrame &frame, cells::const_iterator it) {
		// Update expressions to call list
		frame.expressions.swap(frame.resolved_arguments);
		frame.resolved_arguments.clear();
		frame.exp_it = frame.expressions.cbegin();
		frame.setExpression(*frame.exp_it);
		// Don't move exp_it
		return false;
	}
};

template<> struct SchemeDispatcher<instruction::Proc> {
	static bool dispatch(SchemeFrame &frame, cells::const_iterator it) {
		switch (frame.exp.type) {
			// Proc: a builtin procedure in C++
			// We call this immediately. If it blocks, that is up
			// to the caller.
		case Proc:
			frame.result = frame.exp.proc(frame.resolved_arguments);
			return true;
			// Lambda: a Scheme procedure
			// We create a subframe to run the procedure, along with
			// an environment with arguments set to correct values.
		case Lambda:
		{
			// Arguments
			const cell &arglist = *it; ++it;
			cells arguments;
			switch (arglist.type) {
			case Symbol:
			{
				// Single argument, assign all args as list
				arguments.push_back(arglist);
				// Convert resolved arguments to single list
				cells tmp(frame.resolved_arguments);
				frame.resolved_arguments.clear();
				frame.resolved_arguments.push_back(tmp);
				break;
			}
			case List:
				// List of arguments
				arguments = arglist.list;
				break;
			}
			// Body
			const cell body = *it; ++it;
			// Create environment parented to lambda env and assign arguments
			env_p new_env(new environment(frame.exp.env));
			auto env_arg_it = arguments.cbegin();
			auto res_arg_it = frame.resolved_arguments.cbegin();
			for (; res_arg_it != arguments.cend(); ++env_arg_it, ++res_arg_it)
				new_env->operator[](env_arg_it->val) = *res_arg_it;
			// Create subframe
			frame.subframe_mode = SchemeFrame::Procedure;
			frame.subframe = new SchemeFrame(body, new_env);
			return false;
		}
		default:
			throw std::runtime_error("Dont know how to dispatch proc");
		}
	}
};

bool SchemeFrame::dispatchCall() {
	// all of our arguments are now resolved, dispatch
	cells::const_iterator it = resolved_arguments.cbegin();
	instruction::instruction ins = SchemeInstructionConverter::convert(exp);
	switch (ins) {
	case instruction::If:
		return SchemeDispatcher<instruction::If>::dispatch(*this, it);
	case instruction::Begin:
		return SchemeDispatcher<instruction::Begin>::dispatch(*this, it);
	case instruction::Proc:
		return SchemeDispatcher<instruction::Proc>::dispatch(*this, it);
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
