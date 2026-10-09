#include "test.h"
int main(int argc, char **argv) { return Test_Run(argc > 1 ? argv[1] : "*") ? 1 : 0; }
