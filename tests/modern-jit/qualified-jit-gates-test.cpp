#include "modern040_qualified_jit.h"
#include <cstdlib>

int main(int argc,char **argv) {
    if(argc!=3)return 2;
    const bool expected_qualified=std::atoi(argv[1])!=0;
    const bool expected_opt_in=std::atoi(argv[2])!=0;
    if(aranym_qualified_jit_gate("ARANYM_TEST_QUALIFIED")!=expected_qualified)return 3;
    if(aranym_jit_opt_in("ARANYM_TEST_OPT_IN")!=expected_opt_in)return 4;
    return 0;
}
