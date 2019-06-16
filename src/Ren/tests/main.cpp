
#include <cstdio>

void test_anim();
void test_buffer();
void test_hashmap();
void test_mat();
void test_material();
void test_mesh();
void test_program();
void test_storage();
void test_sparse_array();
void test_string();
void test_texture();
void test_vec();

#include "../GL.h"

int main() {
    test_anim();
    test_buffer();
    test_hashmap();
    test_mat();
    test_material();
    test_mesh();
    test_program();
    test_storage();
    test_sparse_array();
    test_string();
    test_texture();
    test_vec();
    puts("OK");
}