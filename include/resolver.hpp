#ifndef RESOLVER_H_INC
#define RESOLVER_H_INC

#include "arena.hpp"
#include "ast.hpp"

// Resolve resolves all global named type symbols and puts type definitions and
// constants in their proper order.  This is necessary step to ensure that type
// checking and comptime evaluation goes smoothly.
void Resolve(Arena& arena, Module& mod);

#endif