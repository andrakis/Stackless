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
		typedef const typename From &_cell_type;
		typedef typename To _instruction_type;
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

		typename _value_type value;
	};

	template<typename CellType, typename OperationType, typename EnvironmentType>
	struct Frame {
		typedef typename CellType _cell_type;
		typedef typename OperationType _operation_type;
		typedef typename EnvironmentType _env_type;
		typedef typename EnvironmentType::env_p env_p;
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

	template<typename EnvironmentType,typename FrameType>
	struct Implementation {
		typedef typename FrameType _frame_type;
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

	namespace microthreading {
		enum WaitState {
			Stop = 0,
			Run,
		};

		typedef unsigned CycleCount;

		const CycleCount cycles_low = 1;
		const CycleCount cycles_med = 10;
		const CycleCount cycles_hi = 100;

		typedef unsigned thread_id;
		extern thread_id thread_counter;

		template<typename Implementation>
		struct Microthread {
			typedef typename Microthread<Implementation> _thread_type;
			typedef typename Implementation::_frame_type _frame_type;
			typedef typename Implementation::_env_type _env_type;
			typedef typename std::shared_ptr<Implementation> impl_p;

			Microthread(impl_p implementation, const CycleCount cycle_count = cycles_med)
				: thread_id(++thread_counter), impl(implementation), cycles(cycle_count) 
			{
			}

			const typename thread_id thread_id;
			impl_p impl;
			CycleCount cycles;

			typename _frame_type &getCurrentFrame() { return impl->getCurrentFrame(); }
			const typename _frame_type &getCurrentFrame() const { return impl->getCurrentFrame(); }
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
				return _thread_type(cb(args));
			}
			template<class Callback>
			static _thread_type create(Callback cb) {
				return _thread_type(cb());
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
			typedef typename Microthread<Implementation> _thread_type;
			typedef typename _thread_type::impl_p impl_p;
			typedef typename Implementation::_frame_type _frame_type;
			typedef typename Implementation::_env_type _env_type;
			typedef typename std::shared_ptr<_frame_type> frame_p;
			typedef typename std::shared_ptr<_env_type> env_p;
			typedef typename std::map<thread_id,_thread_type> _threads_type;
			typedef typename std::pair<thread_id,_thread_type> _threads_ele;

			MicrothreadManager() : threads() {
			}

			template<typename ArgType, class Callback>
			thread_id start(ArgType args, Callback cb) {
				_thread_type thread(_thread_type::create<ArgType, Callback>(args, cb));
				threads.insert(_threads_ele(thread.thread_id, thread));
				return thread.thread_id;
			}
			template<class Callback>
			thread_id start(Callback cb) {
				_thread_type thread(_thread_type::create<Callback>(cb));
				threads.insert(_threads_ele(thread.thread_id, thread));
				return thread.thread_id;
			}

			const _thread_type &getThread(const thread_id index) const {
				return threads.find(index)->second;
			}
			_thread_type &getThread(const thread_id index) {
				return threads.find(index)->second;
			}
			void remove_thread(const thread_id index) {
				threads.erase(threads.find(index));
			}

			void runThreadToCompletion(const thread_id index, const Threading mode = Single) {
				_thread_type &thread = threads.find(index)->second;
				while (!thread.isResolved()) {
					if(mode == Single)
						// Run single thread
						thread.execute();
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
					for (int cycle_count = thread.cycles; cycle_count > 0; --cycle_count) {
						// Execution cycle
						if (thread.isResolved())
							break;
						thread.execute();
					}
				}
				return threads_run;
			}

		private:
			_threads_type threads;
		};
	}

	namespace timekeeping {
		template<typename TimeType, typename ClockType>
		struct Timekeeper
		{
			template<class Callback>
			static __int64 measure(Callback cb) {
				auto start = ClockType::now();
				cb();
				auto end = ClockType::now();
				return std::chrono::duration_cast<typename TimeType>(end - start).count();
			}
		};

		typedef Timekeeper<typename std::chrono::milliseconds, typename std::chrono::steady_clock> StacklessTimekeeper;
	}
}

