#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

int int_range(char* message, int start, int finnish, int* error) {
    if (start > finnish) {
        *error = -1;
    }
    char buffer[finnish - start]; 
    snprintf(buffer, finnish - start + 1, "%s", message + start);
    return atoi(buffer);
}
