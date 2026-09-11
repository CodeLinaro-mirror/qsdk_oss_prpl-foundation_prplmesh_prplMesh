/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2016-2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef _TLVF_LOGGING_H_
#define _TLVF_LOGGING_H_

#include <easylogging++.h>

#include <iosfwd>
#include <string>
#include <type_traits>

#ifdef TLVF_FLASH_OPTIMIZATION

namespace tlvf {
namespace logging {

// Internal to the logging sink and never printed; a generated printer would put
// a static copy of it in every translation unit including this header.
// enum auto-print skip
enum class eLevel { level_DEBUG, level_INFO, level_WARNING, level_ERROR, level_FATAL };

/**
 * @brief Log message whose streaming operators are defined out of line.
 *
 * Accumulates into a string through non-inline operators, so a call site costs a
 * call instead of an inline el::base::Writer and MessageBuilder chain. The
 * message reaches easylogging++ once, from the destructor in tlvflogging.cpp.
 */
class cMessage {
public:
    explicit cMessage(eLevel level);
    ~cMessage();

    cMessage(const cMessage &) = delete;
    cMessage &operator=(const cMessage &) = delete;

    cMessage &operator<<(const char *value);
    cMessage &operator<<(const std::string &value);
    cMessage &operator<<(char value);
    cMessage &operator<<(bool value);
    cMessage &operator<<(short value);
    cMessage &operator<<(unsigned short value);
    cMessage &operator<<(int value);
    cMessage &operator<<(unsigned int value);
    cMessage &operator<<(long value);
    cMessage &operator<<(unsigned long value);
    cMessage &operator<<(long long value);
    cMessage &operator<<(unsigned long long value);
    cMessage &operator<<(double value);
    cMessage &operator<<(const void *value);

    // In TLVF code int8_t/uint8_t hold lengths, indices and type values, so they
    // are printed as numbers rather than as characters.
    cMessage &operator<<(signed char value);
    cMessage &operator<<(unsigned char value);

    // Enumerators are streamed directly in a few places.
    template <typename T>
    typename std::enable_if<std::is_enum<T>::value, cMessage &>::type operator<<(T value)
    {
        return *this << static_cast<long long>(value);
    }

    // Stream manipulators (std::dec, std::endl) are accepted and ignored: this
    // sink always formats decimally and the line is terminated on flush.
    cMessage &operator<<(std::ios_base &(*)(std::ios_base &)) { return *this; }
    cMessage &operator<<(std::ostream &(*)(std::ostream &)) { return *this; }

private:
    eLevel m_level;
    std::string m_message;
};

} // namespace logging
} // namespace tlvf

// The ## keeps the level token from being macro-expanded, so a stray
// DEBUG/ERROR macro elsewhere cannot break the call sites.
#define TLVF_LOG(level) ::tlvf::logging::cMessage(::tlvf::logging::eLevel::level_##level)

#else // TLVF_FLASH_OPTIMIZATION

// The original definition, verbatim: it already prefixed every message with
// "TLVF: ", so dropping the prefix here would change the log output of a build
// that has the option off.
#define TLVF_LOG(a) (LOG(a) << "TLVF: ")

#endif // TLVF_FLASH_OPTIMIZATION

#endif
