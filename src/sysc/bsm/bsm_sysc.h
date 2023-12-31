#ifndef SRC_SYSC_BSM_BSM_SYSC_H_
#define SRC_SYSC_BSM_BSM_SYSC_H_
#include "sysc/bsm/bsm_sim_context.h"

namespace sc_core {
    extern bsm_sim_context* bsm_create_sim_context(sc_module* top);
}  // namespace sc_core
#endif  // SRC_SYSC_BSM_BSM_SYSC_H_
