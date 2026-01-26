#include <functional>

class Defer {
   public:
    // Acepta cualquier función o lambda
    Defer(std::function<void()> func) : m_func(func) {}

    // El destructor se ejecuta al salir del scope
    ~Defer() {
        if (m_func) m_func();
    }

   private:
    std::function<void()> m_func;
};

// Macro para no tener que inventar nombres de variables
#define DEFER_CONCAT(a, b) a##b
#define DEFER_NAME(a, b)   DEFER_CONCAT(a, b)
#define defer(code)        Defer DEFER_NAME(_defer_, __LINE__)([&]() { code; })