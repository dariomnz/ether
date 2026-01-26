#include <iostream>

#ifdef DEBUG
#define debug_msg(msg) std::cout << msg << std::endl
#else
#define debug_msg(msg)
#endif