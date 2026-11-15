#include "modern040_execution_mode.h"

int main() {
    if(aranym_modern_translation_active(0,0,0,0,0))return 1;
    if(!aranym_modern_translation_active(0x8000,0,0,0,0))return 2;
    if(aranym_modern_translation_active(0,0x8000,0,0,0))return 3;
    if(aranym_modern_translation_active(0,0,0x8000,0,0))return 4;
    if(aranym_modern_translation_active(0,0,0,0x8000,0))return 5;
    if(aranym_modern_translation_active(0,0,0,0,0x8000))return 6;
    if(!aranym_modern_translation_active(0,0,0,0x8004,0))return 7;
    if(!aranym_modern_translation_active(0,0,0,0,0x8004))return 8;
    if(aranym_modern_translation_active(0x4000,0x7fff,0,0,0))return 9;
    return 0;
}
