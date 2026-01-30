#pragma once 

#include <iostream>

#ifdef DEBUG
#define debug_msg(msg) std::cerr << msg << std::endl
#else
#define debug_msg(msg)
#endif