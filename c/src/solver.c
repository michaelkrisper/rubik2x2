#include "solver.h"

const char *solve_status_message(solve_status_t status)
{
    switch (status) {
    case SOLVE_OK:            return "ok";
    case SOLVE_INVALID:       return "not a reachable cube state";
    case SOLVE_NO_SOLUTION:   return "no solution within 14 moves";
    case SOLVE_OUT_OF_MEMORY: return "out of memory";
    }
    return "unknown";
}
