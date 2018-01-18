#pragma once

#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <set>
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

		// TODO: These are no longer required
		//virtual OperationType fetch() = 0;
		//virtual void execute() = 0;
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
		// We use steady clock for thread scheduling as scheduling should
		// not change when time changes.
		using ThreadClock = std::chrono::steady_clock;
		using ThreadTimePoint = std::chrono::steady_clock::time_point;
		using ThreadTimeUnit = std::chrono::milliseconds;

		struct MicrothreadBase {
			const ThreadId thread_id;
			virtual bool isResolved() = 0;
			virtual bool execute() = 0;
			virtual bool executeCycle() = 0;

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
			typedef typename Implementation::_cell_type _cell_type;
			typedef std::queue<_cell_type> _mailbox_type;

			// Whether this thread is being watched, or should be cleaned up automatically
			bool watched = false;

			impl_p impl;
			CycleCount cycles;
			_mailbox_type mailbox;
			ThreadTimePoint sleep_until = ThreadTimePoint::min();

			template<typename Callback, typename Args>
			Microthread(Callback cb, Args args, const ThreadId thread_id, const CycleCount cycle_count = cycles_med)
				: MicrothreadBase(thread_id), cycles(cycle_count), mailbox()
			{
				impl = impl_p(cb(args));
			}

			template<typename Callback>
			Microthread(Callback cb, const ThreadId thread_id, const CycleCount cycle_count = cycles_med)
				: MicrothreadBase(thread_id), cycles(cycle_count), mailbox()
			{
				impl = impl_p(cb());
			}

			_frame_type &getCurrentFrame() { return impl->getCurrentFrame(); }
			const _frame_type &getCurrentFrame() const { return impl->getCurrentFrame(); }
			bool isResolved() { return getCurrentFrame().isResolved(); }
			typename Implementation::_cell_type getResult() const {
				const _frame_type &frame = getCurrentFrame();
				return frame.result;
			}

			bool execute() {
				return executeCycle();
			}

			bool executeCycle() {
				return impl->execute();
			}

			template<typename ArgType, class Callback>
			static _thread_type create(ArgType args, Callback cb, const ThreadId thread_id, const CycleCount cycle_count = cycles_med) {
				return _thread_type(cb, args, thread_id, cycle_count);
			}
			template<class Callback>
			static _thread_type create(Callback cb, const ThreadId thread_id, const CycleCount cycle_count = cycles_med) {
				return _thread_type(cb, thread_id, cycle_count);
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
			typedef typename Implementation::_cell_type _cell_type;
			typedef typename Implementation::_frame_type _frame_type;
			typedef typename Implementation::_env_type _env_type;
			typedef typename std::shared_ptr<_frame_type> frame_p;
			typedef typename std::shared_ptr<_env_type> env_p;
			typedef typename std::map<ThreadId,_thread_type> _threads_type;
			typedef typename std::pair<ThreadId,_thread_type> _threads_ele;
			typedef typename _threads_type::const_iterator _threads_const_iterator;
			typedef typename _threads_type::iterator _threads_iterator;

			// Custom type used to manage scheduling set
			struct SchedulingInformation {
				SchedulingInformation(const ThreadId _thread_id, const ThreadTimePoint &_time_point)
					: thread_id(_thread_id), time_point(_time_point)
				{
				}

				bool operator < (const SchedulingInformation &other) const {
					return time_point < other.time_point;
				}

				const ThreadId thread_id;
				const ThreadTimePoint time_point;
			};
			typedef std::set<SchedulingInformation> _scheduling_type;

			MicrothreadManager() : threads(), current_thread(threads.begin()), scheduling(), thread_counter(0) {
			}

			template<typename ArgType, class Callback>
			ThreadId start(ArgType args, Callback cb, const CycleCount cycle_count = cycles_med) {
				ThreadId thread_id = thread_counter++;
				_thread_type thread(_thread_type::template create<ArgType, Callback>(args, cb, thread_id, cycle_count));
				threads.insert(_threads_ele(thread_id, thread));
				return thread_id;
			}
			template<class Callback>
			ThreadId start(Callback cb, const CycleCount cycle_count = cycles_med) {
				ThreadId thread_id = thread_counter++;
				_thread_type thread(_thread_type::template create<Callback>(cb, thread_id, cycle_count));
				threads.insert(_threads_ele(thread_id, thread));
				return thread_id;
			}

			const _threads_const_iterator getThread(const ThreadId index) const {
				return threads.find(index);
			}
			_threads_iterator getThread(const ThreadId index) {
				return threads.find(index);
			}
			void remove_thread(const ThreadId index) {
				thread_wake(index);
				threads.erase(threads.find(index));
			}

			// Sleep for duration from current time
			void thread_sleep_for(const ThreadId thread_ref, const ThreadTimeUnit &duration) {
				// TODO: Clear any existing timeouts?
				ThreadTimePoint now = ThreadClock::now();
				ThreadTimePoint target = now + duration;
				scheduling.emplace(SchedulingInformation(thread_ref, target));
				getThread(thread_ref)->second.sleep_until = target;
			}
			void thread_sleep_forever(const ThreadId thread_ref) {
				// TODO: Clear any existing timeouts?
				scheduling.emplace(SchedulingInformation(thread_ref, ThreadTimePoint::max()));
				getThread(thread_ref)->second.sleep_until = ThreadTimePoint::max();
			}
			void thread_wake(const ThreadId thread_ref) {
				auto it = getSchedulingFor(getThread(thread_ref));
				if (it != scheduling.end())
					scheduling.erase(*it);
			}

			bool shouldRunThread(_threads_iterator thread) {
				return thread->second.isResolved() || isThreadScheduled(thread);
			}

			bool executeThread(_threads_iterator thread) {
				current_thread = thread;
				bool executed = false;
				for (CycleCount cycle = thread->second.cycles; cycle > 0; --cycle) {
					if (shouldRunThread(thread) == false)
						return executed;
					executed = executed || thread->second.execute();
				}
				return executed;
			}

			void runThreadToCompletion(const ThreadId index, const Threading mode = Single) {
				_threads_iterator thread = threads.find(index);
				if (thread == threads.end())
					return;
				thread->second.watched = true;
				while (!thread->second.isResolved()) {
					if (mode == Single)
						// Run single thread
						executeThread(thread);
					else if(mode == Multi) {
						// Run other threads
						executeThreads();
						if(thread->second.isResolved())
							break;
					}
				}
			}

			int executeThreads() {
				int threads_run = 0;
				bool unwatched_resolved = false;
				for (auto it = threads.begin(); it != threads.end(); ++it) {
					if (it->second.isResolved()) {
						if(it->second.watched == false)
							unwatched_resolved = true;
						continue;
					}
					if (isThreadScheduled(it) == false) {
						continue;
					}
					if(executeThread(it))
						++threads_run;
					if (it->second.watched == false && it->second.isResolved())
						unwatched_resolved = true;
				}
				if(unwatched_resolved)
					idle();
				yield_process(unwatched_resolved, threads_run);
				return threads_run;
			}

			_threads_iterator getCurrentThread() {
				return current_thread;
			}

			bool hasThreads() const {
				return threads.empty() == false;
			}
			typename _threads_type::size_type threadCount() const {
				return threads.size();
			}

			// Send a message to a thread.
			// Returns: true on success, false on thread not existing.
			bool send(const _cell_type &message, const ThreadId thread_id) {
				auto it = threads.find(thread_id);
				if (it == threads.end())
					return false;
				deliver_message(it, message);
				return true;
			}

		protected:
			typename _scheduling_type::iterator getSchedulingFor(_threads_iterator thread) {
				// Find iterator for scheduling info for this thread
				auto it = scheduling.cbegin();
				for (; it != scheduling.cend(); ++it) {
					const SchedulingInformation &info = *it;
					// TODO: This deep check should be moved elsewhere
					if (info.thread_id == thread->second.thread_id) {
						break;
					}
				}
				return it;
			}

			// Check if a thread is scheduled to run.
			bool isThreadScheduled(_threads_iterator thread) {
				// Any scheduling information?
				if (scheduling.empty())
					return true;

				auto it = getSchedulingFor(thread);
				// Any scheduling information?
				if(it == scheduling.cend())
					return true;

				// Check if reached schedule time
				ThreadTimePoint now = ThreadClock::now();
				const SchedulingInformation &info = *it;
				if (info.time_point <= now) {
					// Thread has reached schedule time, remove schedule info
					scheduling.erase(it);
					return true;
				}
				// Thread has not reached schedule
				return false;
			}
			// Idle takes care of cleaning up unwatched processes.
			// This must be done outside the executeThreads main loop, as it alters
			// the threads structure, invalidating iterators.
			virtual void idle() {
				std::vector<typename _threads_type::iterator> cleanup_processes;
				// Find processes to clean up
				for (auto it = threads.begin(); it != threads.end(); ++it) {
					if (it->second.watched == false && it->second.isResolved()) {
						thread_wake(it->second.thread_id);
						cleanup_processes.push_back(it);
					}
				}
				// Clean up the processes we found
				for (auto it = cleanup_processes.begin(); it != cleanup_processes.end(); ++it) {
					threads.erase(*it);
				}
			}
			// To avoid maxing out the CPU whilst no threads are doing anything, this
			// function is called.
			// Since this can be very implementation-dependant, it does nothing in this template.
			virtual void yield_process(bool unwatched_resolved, int threads_run) {
			}
			_threads_type threads;
			_threads_iterator current_thread;
			_scheduling_type scheduling;
			ThreadId thread_counter;
			// Default behaviour
			virtual void deliver_message(_threads_iterator thread, const _cell_type &message) {
				thread->second.mailbox.push(message);
			}
		};

	}

	template<typename EnvironmentType,typename FrameType>
	struct Implementation {
		typedef FrameType _frame_type;
		typedef typename FrameType::_cell_type _cell_type;
		typedef typename FrameType::_operation_type _operation_type;
		typedef typename FrameType::_env_type _env_type;
		typedef typename FrameType::env_p env_p;
		typedef std::queue<typename FrameType::_cell_type> _mailbox_type;

		Implementation(env_p _env) : env(_env), mailbox() {
		}

		env_p env;
		_mailbox_type mailbox;

		bool isResolved() {
			const FrameType &frame = getCurrentFrame();
			return frame.isArgumentsResolved() && frame.isResolved();
		}
		bool execute() {
			FrameType &frame = getCurrentFrame();
			return executeFrame(frame);
		}

		virtual FrameType &getCurrentFrame() = 0;
		virtual bool executeFrame(FrameType &frame) = 0;

		virtual void EVENT_Receive(const _cell_type &message) {
			if (false == onMessage(message))
				mailbox.push(message);
		}

		virtual bool onMessage(const _cell_type &message) {
			std::cerr << "(STUB) Implementation::onMessage" << std::endl;
			return false;
		}

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

