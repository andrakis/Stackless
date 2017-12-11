#pragma once

#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <vector>

namespace stackless {
	template<typename OperationType, typename ArgSizeType, typename ArgsType>
	class InvalidOperation : public std::exception {
	public:
		InvalidOperation(const OperationType instruction, const ArgSizeType argSize, const ArgsType args)
			: exception() {
		}
	};

	template<typename From, typename To>
	struct InstructionConverter {
		typedef const From &_cell_type;
		typedef To _instruction_type;
		//virtual static _instruction_type convert(_cell_type) const = 0;
	};

	template<typename InstructionType>
	struct Dispatcher {
		typedef typename InstructionType::_cell_type _cell_type;
		typedef typename InstructionType::_instruction_type _instruction_type;
	};

	template<typename ListType>
	struct Environment {
		typedef typename ListType::value_type _value_type;
		typedef typename ListType::size_type _size_type;

		_value_type value;
	};

	template<typename CellType, typename OperationType, typename EnvironmentType>
	struct Frame {
		typedef CellType _cell_type;
		typedef OperationType _operation_type;
		typedef EnvironmentType _env_type;
		typedef typename EnvironmentType::_env_p env_p;
		typedef std::list<CellType> StacklessFrameArguments;
		
		Frame(env_p environment) : env(environment) {
		}
		virtual ~Frame() {
		}

		virtual bool isResolved() const = 0;
		virtual bool isArgumentsResolved() const = 0;

		//virtual OperationType fetch() = 0;
		virtual void execute() = 0;

		env_p env;

		CellType result;
	};

	namespace microthreading {
		enum WaitState {
			Stop = 0,
			Run,
		};

		typedef unsigned CycleCount;

		const CycleCount cycles_low = 1;
		const CycleCount cycles_med = 10;
		const CycleCount cycles_hi = 100;

		typedef unsigned ThreadId;
		extern ThreadId thread_counter;

		struct MicrothreadBase {
			const ThreadId thread_id;
			virtual bool isResolved() = 0;
			virtual void execute() = 0;
			virtual void executeCycle() = 0;

		protected:
			MicrothreadBase(ThreadId id) : thread_id(id) {

			}
		};

		template<typename Implementation>
		struct Microthread : public MicrothreadBase {
			typedef Microthread<Implementation> _thread_type;
			typedef typename Implementation::_frame_type _frame_type;
			typedef typename Implementation::_env_type _env_type;
			typedef typename std::shared_ptr<Implementation> impl_p;

			template<typename Callback, typename Args>
			Microthread(Callback cb, Args args, const CycleCount cycle_count = cycles_med)
				: MicrothreadBase(++thread_counter), cycles(cycle_count)
			{
				impl = impl_p(cb(args));
			}

			template<typename Callback>
			Microthread(Callback cb, const CycleCount cycle_count = cycles_med)
				: MicrothreadBase(++thread_counter), cycles(cycle_count)
			{
				impl = impl_p(cb());
			}

			impl_p impl;
			CycleCount cycles;

			_frame_type &getCurrentFrame() { return impl->getCurrentFrame(); }
			const _frame_type &getCurrentFrame() const { return impl->getCurrentFrame(); }
			bool isResolved() { return getCurrentFrame().isResolved(); }
			typename Implementation::_cell_type getResult() const {
				const _frame_type &frame = getCurrentFrame();
				return frame.result;
			}

			void execute() {
				for (CycleCount cycle = cycles; cycle > 0; --cycle) {
					if (isResolved())
						break;
					executeCycle();
				}
			}

			void executeCycle() {
				impl->execute();
			}

			template<typename ArgType, class Callback>
			static _thread_type create(ArgType args, Callback cb) {
				return _thread_type(cb, args);
			}
			template<class Callback>
			static _thread_type create(Callback cb) {
				return _thread_type(cb);
			}
		};
		
		enum Threading {
			// Execute a single thread
			Single,
			// Execute multiple threads
			Multi
		};

		template<typename Implementation>
		struct MicrothreadManager {
			typedef Microthread<Implementation> _thread_type;
			typedef typename _thread_type::impl_p impl_p;
			typedef typename Implementation::_frame_type _frame_type;
			typedef typename Implementation::_env_type _env_type;
			typedef typename std::shared_ptr<_frame_type> frame_p;
			typedef typename std::shared_ptr<_env_type> env_p;
			typedef typename std::map<ThreadId,_thread_type> _threads_type;
			typedef typename std::pair<ThreadId,_thread_type> _threads_ele;

			MicrothreadManager() : threads() {
			}

			template<typename ArgType, class Callback>
			ThreadId start(ArgType args, Callback cb) {
				_thread_type thread(_thread_type::template create<ArgType, Callback>(args, cb));
				threads.insert(_threads_ele(thread.thread_id, thread));
				return thread.thread_id;
			}
			template<class Callback>
			ThreadId start(Callback cb) {
				_thread_type thread(_thread_type::template create<Callback>(cb));
				threads.insert(_threads_ele(thread.thread_id, thread));
				return thread.thread_id;
			}

			const _thread_type &getThread(const ThreadId index) const {
				return threads.find(index)->second;
			}
			_thread_type &getThread(const ThreadId index) {
				return threads.find(index)->second;
			}
			void remove_thread(const ThreadId index) {
				threads.erase(threads.find(index));
			}

			void executeThread(_thread_type &thread) {
				current_thread = &thread;
				thread.execute();
			}

			void runThreadToCompletion(const ThreadId index, const Threading mode = Single) {
				_thread_type &thread = threads.find(index)->second;
				while (!thread.isResolved()) {
					if (mode == Single)
						// Run single thread
						executeThread(thread);
					else if(mode == Multi) {
						// Run other threads
						if (executeThreads() == 0)
							break;
					}
				}
			}

			int executeThreads() {
				int threads_run = 0;
				for (auto it = threads.begin(); it != threads.end(); ++it) {
					_thread_type &thread = it->second;
					if (thread.isResolved())
						continue;
					++threads_run;
					executeThread(thread);
				}
				return threads_run;
			}

			_thread_type *getCurrentThread() {
				return current_thread;
			}

		protected:
			_threads_type threads;
			_thread_type *current_thread;
		};
	}

	template<typename EnvironmentType,typename FrameType>
	struct Implementation {
		typedef FrameType _frame_type;
		typedef typename FrameType::_cell_type _cell_type;
		typedef typename FrameType::_operation_type _operation_type;
		typedef typename FrameType::_env_type _env_type;
		typedef typename FrameType::env_p env_p;

		Implementation(env_p _env) : env(_env) {
		}

		env_p env;

		bool isResolved() {
			const FrameType &frame = getCurrentFrame();
			return frame.isArgumentsResolved() && frame.isResolved();
		}
		void execute() {
			FrameType &frame = getCurrentFrame();
			executeFrame(frame);
		}

		virtual FrameType &getCurrentFrame() = 0;
		virtual void executeFrame(FrameType &frame) = 0;
	};


	namespace timekeeping {
		template<typename TimeType, typename ClockType>
		struct Timekeeper
		{
			template<class Callback>
			static unsigned long long measure(Callback cb) {
				auto start = ClockType::now();
				cb();
				auto end = ClockType::now();
				return std::chrono::duration_cast<TimeType>(end - start).count();
			}
		};

		typedef Timekeeper<typename std::chrono::milliseconds, typename std::chrono::steady_clock> StacklessTimekeeper;
	}
}

