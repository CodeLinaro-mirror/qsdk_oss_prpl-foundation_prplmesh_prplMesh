/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "afc_spectrum_parser.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <easylogging++.h>
#include <limits>

namespace son {
namespace afc_spectrum_parser {

namespace {

std::string unescape_json_string(const std::string &in)
{
    // Controllers may receive escaped JSON from DM / TLV (e.g. \" and \\).
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            const char next = in[i + 1];
            if (next == '"' || next == '\\' || next == '/') {
                out.push_back(next);
                ++i;
                continue;
            }
            if (next == 'n') {
                out.push_back('\n');
                ++i;
                continue;
            }
            if (next == 't') {
                out.push_back('\t');
                ++i;
                continue;
            }
        }
        out.push_back(in[i]);
    }
    return out;
}

// prplMesh is built with -fno-exceptions; avoid std::stoi/stod (they throw).
bool parse_long(const char *begin, const char *end, long &out)
{
    if (begin >= end) {
        return false;
    }
    errno           = 0;
    char *parse_end = nullptr;
    const long v    = std::strtol(begin, &parse_end, 10);
    if (errno != 0 || parse_end == begin || parse_end > end) {
        return false;
    }
    out = v;
    return true;
}

bool parse_double(const char *begin, const char *end, double &out)
{
    if (begin >= end) {
        return false;
    }
    errno           = 0;
    char *parse_end = nullptr;
    const double v  = std::strtod(begin, &parse_end);
    if (errno != 0 || parse_end == begin || parse_end > end) {
        return false;
    }
    out = v;
    return true;
}

bool parse_uint8_array(const std::string &json, size_t array_start, std::vector<uint8_t> &values)
{
    values.clear();
    if (array_start >= json.size() || json[array_start] != '[') {
        return false;
    }

    size_t i = array_start + 1;
    while (i < json.size()) {
        while (i < json.size() &&
               (std::isspace(static_cast<unsigned char>(json[i])) || json[i] == ',')) {
            ++i;
        }
        if (i < json.size() && json[i] == ']') {
            return true;
        }
        if (i >= json.size() || !std::isdigit(static_cast<unsigned char>(json[i]))) {
            return !values.empty();
        }
        size_t end = i;
        while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) {
            ++end;
        }
        long v = 0;
        if (!parse_long(json.data() + i, json.data() + end, v) || v < 0 || v > 255) {
            return false;
        }
        values.push_back(static_cast<uint8_t>(v));
        i = end;
    }
    return !values.empty();
}

bool parse_int8_array(const std::string &json, size_t array_start, std::vector<int8_t> &values)
{
    values.clear();
    if (array_start >= json.size() || json[array_start] != '[') {
        return false;
    }

    size_t i = array_start + 1;
    while (i < json.size()) {
        while (i < json.size() &&
               (std::isspace(static_cast<unsigned char>(json[i])) || json[i] == ',')) {
            ++i;
        }
        if (i < json.size() && json[i] == ']') {
            return true;
        }
        if (i >= json.size()) {
            return !values.empty();
        }
        size_t end = i;
        if (json[end] == '-' || json[end] == '+') {
            ++end;
        }
        while (end < json.size() &&
               (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '.')) {
            ++end;
        }
        // AFC maxEirp may be fractional; store truncated dBm as signed limit.
        double v = 0;
        if (!parse_double(json.data() + i, json.data() + end, v) ||
            v < std::numeric_limits<int8_t>::min() || v > std::numeric_limits<int8_t>::max()) {
            return false;
        }
        values.push_back(static_cast<int8_t>(v));
        i = end;
    }
    return !values.empty();
}

size_t find_key_array(const std::string &json, size_t from, const std::string &key)
{
    const std::string pattern = "\"" + key + "\"";
    size_t pos                = json.find(pattern, from);
    if (pos == std::string::npos) {
        return std::string::npos;
    }
    pos = json.find('[', pos + pattern.size());
    return pos;
}

size_t find_key_number(const std::string &json, size_t from, const std::string &key, uint8_t &value)
{
    const std::string pattern = "\"" + key + "\"";
    size_t pos                = json.find(pattern, from);
    if (pos == std::string::npos) {
        return std::string::npos;
    }
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) {
        return std::string::npos;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    size_t end = pos;
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) {
        ++end;
    }
    if (end == pos) {
        return std::string::npos;
    }
    long v = 0;
    if (!parse_long(json.data() + pos, json.data() + end, v) || v < 0 || v > 255) {
        return std::string::npos;
    }
    value = static_cast<uint8_t>(v);
    return end;
}

} // namespace

bool parse_available_channel_info(const std::string &response_json,
                                  std::vector<sAvailableChannel> &channels)
{
    channels.clear();
    if (response_json.empty()) {
        return false;
    }

    const std::string json = unescape_json_string(response_json);

    // Walk each "availableChannelInfo" occurrence (array of objects).
    const std::string marker = "\"availableChannelInfo\"";
    size_t search_from       = 0;
    while (true) {
        size_t info_key = json.find(marker, search_from);
        if (info_key == std::string::npos) {
            break;
        }
        size_t array_pos = json.find('[', info_key + marker.size());
        if (array_pos == std::string::npos) {
            break;
        }

        // Parse objects inside this array until matching ']'.
        size_t i            = array_pos + 1;
        int bracket_depth   = 1;
        size_t object_start = std::string::npos;

        while (i < json.size() && bracket_depth > 0) {
            if (json[i] == '[') {
                ++bracket_depth;
            } else if (json[i] == ']') {
                --bracket_depth;
                if (bracket_depth == 0) {
                    break;
                }
            } else if (json[i] == '{' && bracket_depth == 1) {
                object_start = i;
            } else if (json[i] == '}' && bracket_depth == 1 && object_start != std::string::npos) {
                const std::string object = json.substr(object_start, i - object_start + 1);

                uint8_t op_class = 0;
                if (find_key_number(object, 0, "globalOperatingClass", op_class) ==
                    std::string::npos) {
                    object_start = std::string::npos;
                    ++i;
                    continue;
                }

                std::vector<uint8_t> channel_cfis;
                std::vector<int8_t> max_eirps;
                const size_t cfi_pos = find_key_array(object, 0, "channelCfi");
                if (cfi_pos == std::string::npos ||
                    !parse_uint8_array(object, cfi_pos, channel_cfis)) {
                    object_start = std::string::npos;
                    ++i;
                    continue;
                }

                const size_t eirp_pos = find_key_array(object, 0, "maxEirp");
                if (eirp_pos != std::string::npos) {
                    parse_int8_array(object, eirp_pos, max_eirps);
                }

                for (size_t idx = 0; idx < channel_cfis.size(); ++idx) {
                    sAvailableChannel entry;
                    entry.operating_class = op_class;
                    entry.channel         = channel_cfis[idx];
                    if (idx < max_eirps.size()) {
                        entry.max_eirp_dbm   = max_eirps[idx];
                        entry.max_eirp_valid = true;
                    }
                    channels.push_back(entry);
                }
                object_start = std::string::npos;
            }
            ++i;
        }

        search_from = i;
    }

    if (channels.empty()) {
        LOG(WARNING) << "AFC response JSON contained no availableChannelInfo entries";
        return false;
    }

    LOG(INFO) << "Parsed " << channels.size()
              << " AFC available channel entries from inquiry response";
    return true;
}

bool select_transmit_power_limit_dbm(const std::vector<sAvailableChannel> &channels,
                                     uint8_t preferred_channel, int8_t &limit_dbm)
{
    bool found_any  = false;
    int8_t min_eirp = std::numeric_limits<int8_t>::max();

    for (const auto &entry : channels) {
        if (!entry.max_eirp_valid) {
            continue;
        }
        if (preferred_channel != 0 && entry.channel == preferred_channel) {
            limit_dbm = entry.max_eirp_dbm;
            return true;
        }
        min_eirp  = std::min(min_eirp, entry.max_eirp_dbm);
        found_any = true;
    }

    if (!found_any) {
        return false;
    }

    limit_dbm = min_eirp;
    return true;
}

} // namespace afc_spectrum_parser
} // namespace son
