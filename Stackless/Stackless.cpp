// Stackless.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

namespace stackless {
	namespace microthreading {
		thread_id thread_counter = 0;
	}
}

namespace implementations {
	namespace brainfck {
		void BFTest();
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
    return 0;
}

