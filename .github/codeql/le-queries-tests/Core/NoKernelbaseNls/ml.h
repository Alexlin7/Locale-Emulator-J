// Stands in for the upstream ml library, which the query ignores.
#include "winstub.h"
inline UINT MlGetAcp() { return GetACP(); }
