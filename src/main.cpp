#include <iostream>
#include <fstream>

#include "parser.hpp"
#include "checker.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "error: usage: berry <filename>\n");
        return 1;
    }

    char* file_name = argv[1];

    std::ifstream file;
    file.open(file_name);
    if (!file) {
        fprintf(stderr, "error: failed to open file: %s\n", strerror(errno));
        return 1;
    }

    Module mod { 0, "main" };
    SourceFile src_file { 0, &mod, file_name, file_name };
    
    Arena arena;
    Parser p(arena, file, src_file);
    p.ParseFile();

    if (ErrorCount() == 0) {
        Checker c(arena, src_file);
        
        for (auto& def : src_file.defs) {
            def->Check(c);

            def->Print();
            std::cout << "\n\n";
        }

        std::cout.flush();
    }

    


    file.close();
}