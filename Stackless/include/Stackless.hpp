#pragma once

#include <chrono>
#include <list>
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
	struct StacklessInstructionConverter {
		static To convert(const From &cell) {
			return (To)cell;
		}
	};

	template<typename ListType>
	struct StacklessEnvironment {
		typedef typename ListType::value_type value_type;
		typedef typename ListType::size_type size_type;

		typename value_type value;
	};

	template<typename CellType, typename OperationType, typename EnvironmentType>
	struct StacklessFrame {
		typedef typename CellType cell_type;
		typedef typename OperationType operation_type;
		typedef typename EnvironmentType env_type;
		typedef typename EnvironmentType::env_p env_p;
		typedef typename StacklessFrame<CellType,OperationType,EnvironmentType> Frame;
		typedef std::list<CellType> StacklessFrameArguments;
		
		StacklessFrame(env_p environment) : env(environment) {
		}
		virtual ~StacklessFrame() {
		}

		virtual bool isResolved() const = 0;
		virtual bool isArgumentsResolved() const = 0;

		virtual OperationType fetch() = 0;
		virtual void execute() = 0;

		env_p env;

		CellType result;
	};

	template<typename EnvironmentType,typename FrameType>
	struct StacklessImplementation {
		typedef typename FrameType frame_type;
		typedef typename FrameType::cell_type cell_type;
		typedef typename FrameType::operation_type operation_type;
		typedef typename FrameType::env_type env_type;
		typedef typename FrameType::env_p env_p;

		StacklessImplementation(env_p _env) : env(_env) {
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

		const CycleCount cycles_low = 10;
		const CycleCount cycles_med = 100;
		const CycleCount cycles_hi = 1000;

		typedef unsigned thread_id;
		extern thread_id thread_counter;

		template<typename Implementation>
		struct Microthread {
			typedef typename Microthread<Implementation> thread_type;
			typedef typename Implementation::frame_type frame_type;
			typedef typename Implementation::env_type env_type;
			typedef typename std::shared_ptr<Implementation> impl_p;

			Microthread(impl_p implementation, const CycleCount cycle_count = cycles_med)
				: thread_id(++thread_counter), impl(implementation), cycles(cycle_count) 
			{
			}

			const typename thread_id thread_id;
			impl_p impl;
			CycleCount cycles;

			typename frame_type &getCurrentFrame() { return impl->getCurrentFrame(); }
			bool isResolved() { return getCurrentFrame().isResolved(); }
			typename Implementation::cell_type getResult() const { return currentFrame().result; }

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
			static thread_type create(ArgType args, Callback cb) {
				return thread_type(cb(args));
			}
		};

		template<typename Implementation>
		struct MicrothreadManager {
			typedef typename Microthread<Implementation> thread_type;
			typedef typename thread_type::impl_p impl_p;
			typedef typename Implementation::frame_type frame_type;
			typedef typename Implementation::env_type env_type;
			typedef typename std::shared_ptr<frame_type> frame_p;
			typedef typename std::shared_ptr<env_type> env_p;
			typedef typename std::vector<thread_type> threads_type;

			MicrothreadManager() : threads() {
			}

			template<typename ArgType, class Callback>
			thread_id start(ArgType args, Callback cb) {
				thread_type thread(thread_type::create<ArgType, Callback>(args, cb));
				threads.push_back(thread);
				return thread.thread_id;
			}

			int executeThreads() {
				int threads_run = 0;
				for (auto it = threads.begin(); it != threads.end(); ++it) {
					thread_type &thread = *it;
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
			threads_type threads;
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

