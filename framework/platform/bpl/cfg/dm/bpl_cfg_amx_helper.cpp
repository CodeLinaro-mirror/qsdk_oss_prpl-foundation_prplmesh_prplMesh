#include "bpl_cfg_amx_helper.h"

namespace beerocks {
namespace bpl {

AmbiorixVariantSmartPtr get_controller_dm() { return m_ambiorix_cl.get_object(CONTROLLER_ROOT_DM); }

AmbiorixVariantSmartPtr get_agent_dm() { return m_ambiorix_cl.get_object(AGENT_ROOT_DM); }

} // namespace bpl
} // namespace beerocks
