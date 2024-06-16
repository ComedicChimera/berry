#ifndef ESCAPE_H_INC
#define ESCAPE_H_INC

#include "symbol.hpp"

class Escape {
    Module& mod;

public:
    Escape(Module& mod_);
    void EscapeModule();
};

#endif