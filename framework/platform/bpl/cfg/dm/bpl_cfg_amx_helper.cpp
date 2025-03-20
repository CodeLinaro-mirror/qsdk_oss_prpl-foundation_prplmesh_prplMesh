#include "bpl_cfg_amx_helper.h"

#include "bpl_cfg_pwhm.h"

#include <mapf/common/logger.h>

namespace beerocks {
namespace bpl {

constexpr const char *CONTROLLER_CONFIG_PATH = CONTROLLER_ROOT_DM ".Configuration";
constexpr const char *AGENT_CONFIG_PATH      = AGENT_ROOT_DM ".Configuration";

template <typename T> bool set_controller_config_param(const std::string &name, const T &value)
{
    if (!amb_ptr) {
        MAPF_ERR("set_controller_config_param: controller DM does not exist");
        return false;
    }
    return amb_ptr->set(CONTROLLER_CONFIG_PATH, name, value);
}

template <typename T> bool set_agent_config_param(const std::string &name, const T &value)
{
    if (!amb_ptr) {
        MAPF_ERR("set_agent_config_param: agent DM does not exist");
        return false;
    }
    return amb_ptr->set(AGENT_CONFIG_PATH, name, value);
}

template <typename T> bool read_controller_config_param(const std::string &name, T &value)
{
    if (!amb_ptr) {
        MAPF_ERR("read_controller_config_param: controller DM does not exist");
        return false;
    }
    return amb_ptr->read_param(CONTROLLER_CONFIG_PATH, name, &value);
}

template <typename T> bool read_agent_config_param(const std::string &name, T &value)
{
    if (!amb_ptr) {
        MAPF_ERR("read_agent_config_param: agent DM does not exist");
        return false;
    }
    return amb_ptr->read_param(AGENT_CONFIG_PATH, name, &value);
}

// -----------------------------
// Explicit template instantiations
// -----------------------------

template bool set_controller_config_param<int>(const std::string &, const int &);
template bool set_controller_config_param<long>(const std::string &, const long &);
template bool set_controller_config_param<unsigned int>(const std::string &, const unsigned int &);
template bool set_controller_config_param<bool>(const std::string &, const bool &);
template bool set_controller_config_param<std::string>(const std::string &, const std::string &);

template bool set_agent_config_param<int>(const std::string &, const int &);
template bool set_agent_config_param<long>(const std::string &, const long &);
template bool set_agent_config_param<unsigned int>(const std::string &, const unsigned int &);
template bool set_agent_config_param<bool>(const std::string &, const bool &);
template bool set_agent_config_param<std::string>(const std::string &, const std::string &);

template bool read_controller_config_param<int>(const std::string &, int &);
template bool read_controller_config_param<long>(const std::string &, long &);
template bool read_controller_config_param<unsigned int>(const std::string &, unsigned int &);
template bool read_controller_config_param<bool>(const std::string &, bool &);
template bool read_controller_config_param<std::string>(const std::string &, std::string &);

template bool read_agent_config_param<int>(const std::string &, int &);
template bool read_agent_config_param<long>(const std::string &, long &);
template bool read_agent_config_param<unsigned int>(const std::string &, unsigned int &);
template bool read_agent_config_param<bool>(const std::string &, bool &);
template bool read_agent_config_param<std::string>(const std::string &, std::string &);

} // namespace bpl
} // namespace beerocks
