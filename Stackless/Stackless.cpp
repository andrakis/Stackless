// Stackless.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

namespace stackless {
	namespace microthreading {
		ThreadId thread_counter = 0;
	}
}

namespace implementations {
	namespace brainfck {
		void BFTest();
	}
	namespace scheme {
		void scheme_test();
		unsigned scheme_complete_test();
	}
}
namespace references {
	namespace scheme {
		unsigned scheme_complete_test();
	}
}

int main()
{
	implementations::brainfck::BFTest();
	references::scheme::scheme_complete_test();
	implementations::scheme::scheme_test();
	implementations::scheme::scheme_complete_test();
    return 0;
}

