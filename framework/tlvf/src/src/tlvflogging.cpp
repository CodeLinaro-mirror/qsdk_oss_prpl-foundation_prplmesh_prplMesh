/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <tlvf/tlvflogging.h>

#include <cstdio>

#ifdef TLVF_FLASH_OPTIMIZATION

namespace tlvf {
namespace logging {

cMessage::cMessage(eLevel level) : m_level(level) { m_message.reserve(128); }

cMessage::~cMessage()
{
    switch (m_level) {
    case eLevel::level_DEBUG:
        LOG(DEBUG) << "TLVF: " << m_message;
        break;
    case eLevel::level_INFO:
        LOG(INFO) << "TLVF: " << m_message;
        break;
    case eLevel::level_WARNING:
        LOG(WARNING) << "TLVF: " << m_message;
        break;
    case eLevel::level_FATAL:
        LOG(FATAL) << "TLVF: " << m_message;
        break;
    case eLevel::level_ERROR:
    default:
        LOG(ERROR) << "TLVF: " << m_message;
        break;
    }
}

cMessage &cMessage::operator<<(const char *value)
{
    if (value) {
        m_message.append(value);
    }
    return *this;
}

cMessage &cMessage::operator<<(const std::string &value)
{
    m_message.append(value);
    return *this;
}

cMessage &cMessage::operator<<(char value)
{
    m_message.push_back(value);
    return *this;
}

cMessage &cMessage::operator<<(bool value)
{
    m_message.append(value ? "1" : "0");
    return *this;
}

cMessage &cMessage::operator<<(short value)
{
    m_message.append(std::to_string(value));
    return *this;
}

cMessage &cMessage::operator<<(unsigned short value)
{
    m_message.append(std::to_string(value));
    return *this;
}

cMessage &cMessage::operator<<(int value)
{
    m_message.append(std::to_string(value));
    return *this;
}

cMessage &cMessage::operator<<(unsigned int value)
{
    m_message.append(std::to_string(value));
    return *this;
}

cMessage &cMessage::operator<<(long value)
{
    m_message.append(std::to_string(value));
    return *this;
}

cMessage &cMessage::operator<<(unsigned long value)
{
    m_message.append(std::to_string(value));
    return *this;
}

cMessage &cMessage::operator<<(long long value)
{
    m_message.append(std::to_string(value));
    return *this;
}

cMessage &cMessage::operator<<(unsigned long long value)
{
    m_message.append(std::to_string(value));
    return *this;
}

cMessage &cMessage::operator<<(double value)
{
    m_message.append(std::to_string(value));
    return *this;
}

cMessage &cMessage::operator<<(signed char value)
{
    m_message.append(std::to_string(static_cast<int>(value)));
    return *this;
}

cMessage &cMessage::operator<<(unsigned char value)
{
    m_message.append(std::to_string(static_cast<unsigned int>(value)));
    return *this;
}

cMessage &cMessage::operator<<(const void *value)
{
    char buffer[2 + sizeof(void *) * 2 + 1];
    std::snprintf(buffer, sizeof(buffer), "%p", value);
    m_message.append(buffer);
    return *this;
}

} // namespace logging
} // namespace tlvf

#endif // TLVF_FLASH_OPTIMIZATION
