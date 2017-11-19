// Stackless.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

namespace stackless {
	namespace microthreading {
		thread_id thread_counter = 0;
	}
}

void BFTest();
int main()
{
	BFTest();
    return 0;
}

