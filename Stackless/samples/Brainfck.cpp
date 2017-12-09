#include "stdafx.h"

#include <assert.h>
#include <chrono>
#include <exception>
#include <list>
#include <memory>
#include <stdio.h>
#include <tuple>
#include <vector>

#include <iostream>

const bool verbose = false;

using namespace stackless;
using namespace stackless::microthreading;
using namespace stackless::timekeeping;

namespace implementations {
namespace brainfck {

enum BFOperations {
	InvalidOperation,
	CellRight = '>',
	CellLeft = '<',
	CellIncrement = '+',
	CellDecrement = '-',
	CellWhile = '[',
	CellEndWhile = ']',
	CellPrint = '.',
	CellRead = ',',
};

typedef char BFCell;
typedef std::vector<BFCell> BFList;
typedef BFList::iterator BFList_iterator;
typedef BFList::const_iterator BFList_const_iterator;

// Must be a power of 2
const size_t BFMEMSIZE = 1 << 15; // 32768

//typedef InstructionConverter<BFCell, BFOperations> BFInstructionConverter;
struct BFInstructionConverter : public InstructionConverter<BFCell, BFOperations> {
	static _instruction_type convert(_cell_type cell) {
		return (_instruction_type)cell;
	}
};

struct BFEnvironment : public Environment<BFList> {
	typedef std::shared_ptr<BFEnvironment> env_p;
	typedef env_p _env_p;

	BFEnvironment(env_p outer = nullptr, const typename BFList::size_type memsize = BFMEMSIZE)
	: _outer(outer), tape(memsize), _memsize_max(memsize - 1)
	{
		assert(("memsize must be greater than 0", memsize != 0));
		// Ensure memsize is a power of 2
		assert(("memsize must be a power of 2", 0 == (memsize & (memsize - 1))));
	}

	BFList_iterator begin() { return tape.begin(); }
	BFList_const_iterator cbegin() const { return tape.cbegin(); }
	BFList_iterator end() { return tape.end(); }
	BFList_const_iterator cend() const { return tape.cend(); }

	typedef std::vector<BFOperations> code_type;

	template<typename ListType>
	void assignCode(ListType data) {
		typename ListType::iterator datptr = data.begin();
		for (; datptr != data.end(); ++datptr) {
			const char ch = *datptr;
			code.push_back((BFOperations)ch);
		}
	}

	_size_type ipValue() const {
		return wrap(ip);
	}
	_size_type mpValue() const {
		return wrap(mp);
	}
	_size_type ip = 0;
	_size_type mp = 0;

	_size_type wrap(const _size_type pos) const {
		// _memsize_max is length of _mem -1.
		return pos & _memsize_max;
	}

	_size_type mem_size() const {
		return _memsize_max + 1;
	}
	typename code_type::size_type code_size() const {
		return code.size();
	}

	BFList tape;
	code_type code;

private:
	env_p _outer;
	_size_type _memsize_max;

};

// An unused arguments type
typedef unsigned BFArgs;
typedef unsigned BFArgsSize;

typedef Frame<BFCell, BFOperations, BFEnvironment> BFStacklessFrame;

struct BFFrame : public BFStacklessFrame {
	BFFrame(env_p environment) : BFStacklessFrame(environment) {}
	void execute() {
		dispatch();
	}
	void dispatch();
	// Fetch current instruction
	BFOperations fetch() {
		return BFInstructionConverter::convert(env->code[env->ip]);
	}
	bool isResolved() const {
		// Execution stops if *ip = 0
		return env->ip == env->code_size() || env->code[env->ip] == 0;
	}
	bool isArgumentsResolved() const {
		// This frame has no arguments
		return true;
	}
};

struct BFImplementation : public Implementation<BFEnvironment,BFFrame> {
	// Create the single frame we'll reuse throughout execution
	BFImplementation(env_p _env) : Implementation(_env), frame(BFFrame(_env)) {
	}
	BFFrame &getCurrentFrame() {
		return frame;
	}
	void executeFrame(BFFrame &frame) {
		frame.dispatch();
	}
private:
	BFFrame frame;
};

// Unimplemented opcode handler
template<BFOperations Operation> struct BFDispatcher {
	static void dispatch(BFFrame &frame, BFArgs &args) {
		if (verbose) std::cerr << "Unimplemented opcode: " << (char)frame.env->ip << "\n";
		throw stackless::InvalidOperation<BFOperations, int, BFArgs>(Operation, 0, args);
	}
};
template<> struct BFDispatcher<CellRight> {
	static void dispatch(BFFrame &frame, BFArgs &args) {
		++frame.env->mp;
		if(verbose) std::cerr << "> success, mp=" << frame.env->mp << "\n";
	}
};
template<> struct BFDispatcher<CellLeft> {
	static void dispatch(BFFrame &frame, BFArgs &args) {
		--frame.env->mp;
		if(verbose) std::cerr << "< success, mp=" << frame.env->mp << "\n";
	}
};
template<> struct BFDispatcher<CellIncrement> {
	static void dispatch(BFFrame &frame, BFArgs &args) {
		++frame.env->tape[frame.env->mp];
		if(verbose) std::cerr << "+ success, *mp=" << (int)frame.env->tape[frame.env->mp] << "\n";
	}
};
template<> struct BFDispatcher<CellDecrement> {
	static void dispatch(BFFrame &frame, BFArgs &args) {
		--frame.env->tape[frame.env->mp];
		if(verbose) std::cerr << "- success, *mp=" << (int)frame.env->tape[frame.env->mp] << "\n";
	}
};
template<> struct BFDispatcher<CellPrint> {
	static void dispatch(BFFrame &frame, BFArgs &args) {
		std::cout << (char)frame.env->tape[frame.env->mp];
	}
};
template<> struct BFDispatcher<CellWhile> {
	static void dispatch(BFFrame &frame, BFArgs &args) {
		if (frame.env->tape[frame.env->mp] == 0) {
			unsigned nesting = 1;
			while (nesting > 0) {
				const BFCell &ch = frame.env->code[++frame.env->ip];
				if (ch == CellWhile) ++nesting;
				else if (ch == CellEndWhile) --nesting;
			}
		}
		if(verbose) std::cerr << "[ success, mp=" << frame.env->mp << "\n";
	}
};
template<> struct BFDispatcher<CellEndWhile> {
	static void dispatch(BFFrame &frame, BFArgs &args) {
		if (frame.env->tape[frame.env->mp] != 0) {
			unsigned nesting = 1;
			--frame.env->ip;
			while (nesting > 0) {
				const BFCell &ch = frame.env->code[--frame.env->ip];
				if (ch == CellEndWhile) ++nesting;
				else if (ch == CellWhile) --nesting;
			}
		}
		if(verbose) std::cerr << "] success, *mp=" << frame.env->mp << "\n";
	}
};

struct BFFrameDispatcher {
	static void dispatch(BFFrame &frame, BFArgsSize argsSize, BFArgs &args) {
		// Master dispatcher
		const BFOperations operation = frame.fetch();
		
		if(verbose) std::cerr << "Fetch at " << frame.env->ipValue() << " = " << operation << ", mp = " << frame.env->mpValue() << "\n";

		const BFOperations &instruction = frame.fetch();
		++frame.env->ip;

		switch (instruction) {
		case CellRight:
			return BFDispatcher<CellRight>::dispatch(frame, args);
		case CellLeft:
			return BFDispatcher<CellLeft>::dispatch(frame, args);
		case CellIncrement:
			return BFDispatcher<CellIncrement>::dispatch(frame, args);
		case CellDecrement:
			return BFDispatcher<CellDecrement>::dispatch(frame, args);
		case CellWhile:
			return BFDispatcher<CellWhile>::dispatch(frame, args);
		case CellEndWhile:
			return BFDispatcher<CellEndWhile>::dispatch(frame, args);
		case CellPrint:
			return BFDispatcher<CellPrint>::dispatch(frame, args);
		case CellRead:
			return BFDispatcher<CellRead>::dispatch(frame, args);
		default:
			// Do nothing, as per BF rules
			break;
		}
	}
};

void BFFrame::dispatch() {
	static BFArgsSize argsSize = 0;
	static BFArgs args = 0;
	BFFrameDispatcher::dispatch(*this, argsSize, args);
}

typedef MicrothreadManager<BFImplementation> BFMicrothreadManager;

void BFTest() {
	// Hello world application
	std::string hello_world = "\
	+++++ +++          Set Cell #0 to 8                                      \
	[                                                                         \
	   >++++           Add 4 to Cell #1; this will always set Cell #1 to 4    \
	   [               as the cell will be cleared by the loop                \
	       >++         Add 4*2 to Cell #2                                     \
	       >+++        Add 4*3 to Cell #3                                     \
	       >+++        Add 4*3 to Cell #4                                     \
	       >+          Add 4 to Cell #5                                       \
	       <<<<-       Decrement the loop counter in Cell #1                  \
	   ]               Loop till Cell #1 is zero                              \
	   >+              Add 1 to Cell #2                                       \
	   >+              Add 1 to Cell #3                                       \
	   >-              Subtract 1 from Cell #4                                \
	   >>+             Add 1 to Cell #6                                       \
	   [<]             Move back to the first zero cell you find; this will   \
	                   be Cell #1 which was cleared by the previous loop      \
	   <-              Decrement the loop Counter in Cell #0                  \
	]                  Loop till Cell #0 is zero                             \
	                                                                          \
	The result of this is:                                                    \
	Cell No :   0   1   2   3   4   5   6                                     \
	Contents:   0   0  72 104  88  32   8                                     \
	Pointer :   ^                                                             \
	                                                                          \
	>>.                     Cell #2 has value 72 which is 'H'                 \
	>---.                   Subtract 3 from Cell #3 to get 101 which is 'e'   \
	+++++ ++..+++.          Likewise for 'llo' from Cell #3                   \
	>>.                     Cell #5 is 32 for the space                       \
	<-.                     Subtract 1 from Cell #4 for 87 to give a 'W'      \
	<.                      Cell #3 was set to 'o' from the end of 'Hello'    \
	+++.----- -.----- ---.  Cell #3 for 'rl' and 'd'                          \
	>>+.                    Add 1 to Cell #5 gives us an exclamation point    \
	>++.                    And finally a newline from Cell #6                \
	";

	BFMicrothreadManager manager;

	auto duration = StacklessTimekeeper::measure([&manager, &hello_world]() {
		// Create a few different instances
		for (int thread_id = 0; thread_id < 5; ++thread_id) {
			manager.start<const std::string &>(hello_world, [](auto code, auto thread_ptr) {
				BFImplementation::env_p env(new BFEnvironment());
				env->assignCode(code);
				BFMicrothreadManager::impl_p impl(new BFImplementation(env));
				return impl;
			});
		}
	});
	std::cout << "Setup completed in " << duration << "ms" << std::endl;

	duration = StacklessTimekeeper::measure([&manager]() {
		// Execution loop
		while (manager.executeThreads() > 0);
	});
	std::cout << "Run completed in " << duration << "ms" << std::endl;
}

struct BFInterpreterState {
	BFInterpreterState() {

	}
private:
	BFEnvironment _top;
};
}
}
