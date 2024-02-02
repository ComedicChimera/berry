#include <iostream>

#include "arena.hpp"

struct LLNode {
    int value;
    LLNode* next;
};

void testArenaBasicAlloc() {
    printf("\nBasic Alloc:\n\n");

    Arena arena;

    LLNode head;
    head.value = 0;

    LLNode* curr = &head;
    LLNode* next;
    for (int i = 0; i < 100; i++) {
        next = (LLNode*)arena.Alloc(sizeof(LLNode));
        next->value = i + 1;
        next->next = nullptr;

        curr->next = next;

        curr = next;
    }

    curr = &head;
    while (curr != nullptr) {
        printf("%d\n", curr->value);
        curr = curr->next;
    }
}

#define N_MANY_ALLOC ((10 * 1024 * 1024))

struct Vec2 {
    size_t x, y;
};

void testArenaManyAlloc() {
    printf("\nMany Alloc:\n\n");

    Arena arena;

    std::vector<Vec2*> test_vecs;
    for (size_t i = 0; i < N_MANY_ALLOC; i++) {
        Vec2* v = (Vec2*)arena.Alloc(sizeof(Vec2));
        v->x = (i % 340);
        v->y = (i % 290);

        if (i % 100000 == 0) {
            test_vecs.push_back(v);
        }
    }

    for (auto* v : test_vecs) {
        printf("(%zu, %zu)\n", v->x, v->y);
    }
}

#define ARENA_BIG_ALLOC_SIZE ((10 * 1024 * 1024))

void testArenaBigAlloc() {
    printf("\nBig Alloc:\n\n");

    Arena arena;
    arena.Alloc(10);

    char* data = (char*)arena.Alloc(ARENA_BIG_ALLOC_SIZE);
    *data = 'a';
    *(data + ARENA_BIG_ALLOC_SIZE - 1) = 'z';

    printf("(%c, %c)\n", *data, *(data + ARENA_BIG_ALLOC_SIZE - 1));
}

struct TestData {
    int x, y, z;
    std::string value;
};

void testArenaConstruct() {
    Arena arena;

    TestData* data = arena.New<TestData>(1, 2, 3, "Hello");

    printf("\nConstruct:\n\n(%d, %d, %d, %s)\n", data->x, data->y, data->z, data->value.c_str());
}

void testArenaMoveTo() {
    Arena arena;

    printf("\nMove to Arena:\n\n");

    std::string msg = "Hello, there!\n";

    auto msg_view = arena.MoveStr(std::move(msg));

    printf("msg len = %zu\n", msg.size());
    std::cout << "view message = " << msg_view << '\n';

    std::vector<std::string> vec;
    for (int i = 0; i < 10; i++) {
        vec.push_back(std::to_string(i));
    }

    auto vec_view = arena.MoveVec(std::move(vec));

    printf("vec len = %zu\n", vec.size());

    for (auto& item : vec_view) {
        std::cout << item << '\n';
    }
}

void testArenaAll() {
    testArenaBasicAlloc();
    testArenaManyAlloc();
    testArenaBigAlloc();
    testArenaConstruct();
    testArenaMoveTo();
}
