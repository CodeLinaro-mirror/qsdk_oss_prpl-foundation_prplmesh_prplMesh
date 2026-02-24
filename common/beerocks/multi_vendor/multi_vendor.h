/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <tlvf/CmduMessageRx.h>
#include <tlvf/CmduMessageTx.h>
#include <vector>

namespace multi_vendor {

/**
 * @brief Lightweight registry and dispatcher for vendor-specific TLV handlers.
 *
 * Design:
 *  - Message type to index mapping is implemented in the .cpp (switch).
 *  - For each index, we keep a flat vector of handler function pointers.
 *  - Registration occurs once during program initialization (static constructors).
 *  - add_vs_tlv() is the hot path: it linearly executes the handlers for a message type.
 *
 * Notes:
 *  - We intentionally use a raw function pointer type (not std::function) to avoid
 *    allocations and type erasure overhead on the hot path. s_handlers uses a literal
 *    size (msgTypeCount) to keep the array type a compile-time constant.
 *
 * How to add a vendor:
 *  - Derive a new class from 'multi_vendor::tlvf_handler' in your vendor module.
 *  - In its constructor, call 'register_handler()' for each message type that should
 *    include vendor-specific TLVs.
 *  - Instantiate an object of that class inside an unnamed namespace in your '.cpp'
 *    file so its constructor runs automatically at program startup and registers handlers.
 *
 *  For an example implementation, see:
 *    'common/beerocks/multi_vendor/example/src/multi_vendor_example.cpp'
 */
class tlvf_handler {
public:
    // Handler signature: append vendor-specific TLVs to the given CMDU. Return true on success.
    typedef bool (*tlv_function_t)(ieee1905_1::CmduMessageTx &);

    // Total number of supported message types. See to_index method in .cpp
    static constexpr std::size_t msgTypeCount = 89;

    /**
     * @brief Register a handler for a given IEEE 1905.1 message type.
     *
     * Thread safety: internally synchronized; safe to call from static constructors.
     *
     * @param msg_type IEEE 1905.1 message type
     * @param fn       Handler function pointer
     */
    static void register_handler(ieee1905_1::eMessageType msg_type, tlv_function_t fn);

    /**
     * @brief Execute all registered handlers for the given message type.
     *
     * Handlers are executed in registration order.
     * Each handler returns true on success or false on failure.
     * A failing handler only triggers a warning, the remaining handlers continue to run.
     *
     * The function returns true unless an internal error occurs.
     * If no handler is registered for the given type, the function returns true.
     *
     * @param cmdu_tx  CMDU message to which vendor-specific TLVs will be appended
     * @param msg_type IEEE 1905.1 message type
     * @return true on success, false if any handler failed
     */
    static bool add_vs_tlv(ieee1905_1::CmduMessageTx &cmdu_tx, ieee1905_1::eMessageType msg_type);

    // Handler signature for parsing vendor-specific TLVs: parse from CMDU and return parsed TLV.
    typedef std::shared_ptr<BaseClass> (*tlv_parser_function_t)(ieee1905_1::CmduMessageRx &);

    /**
     * @brief Register a parser for vendor-specific TLVs for a given IEEE 1905.1 message type.
     *
     * Thread safety: internally synchronized; safe to call from static constructors.
     *
     * @param msg_type IEEE 1905.1 message type
     * @param fn       Parser function pointer
     */
    static void register_parser(ieee1905_1::eMessageType msg_type, tlv_parser_function_t fn);

    /**
     * @brief Execute all registered parsers for the given message type.
     *
     * Parsers are executed in registration order.
     * The function returns the first successfully parsed TLV (non-null result).
     * If no parser handles the TLV, returns nullptr to allow fallback to generic parsing.
     *
     * @param cmdu_rx  Received CMDU message containing vendor-specific TLVs
     * @param msg_type IEEE 1905.1 message type
     * @return Parsed vendor-specific TLV if successful, nullptr otherwise
     */
    static std::shared_ptr<BaseClass> parse_vs_tlv(ieee1905_1::CmduMessageRx &cmdu_rx,
                                                   ieee1905_1::eMessageType msg_type);

    /**
     * @brief Safely read vendor-specific data from TLV buffer with comprehensive bounds checking
     *
     * This helper validates buffer bounds before extracting vendor-specific fields from the TLV payload.
     * It prevents out-of-bounds memory access from malformed or truncated TLVs.
     *
     * TLV structure: [type:1][length:2][oui:3][vendor_data:variable]
     *
     * @param cmdu_rx            Received CMDU message containing the TLV
     * @param vendor_data_out    Output buffer to receive vendor-specific data (can be nullptr if only OUI needed)
     * @param vendor_data_size   Size of data to read in bytes (e.g., 2 for uint16_t, 4 for uint32_t)
     * @param vendor_data_offset Offset within vendor data section, in bytes (0 = immediately after OUI)
     * @param oui_out            Optional output parameter to receive OUI value in host byte order (can be nullptr)
     * @return true if validation succeeded and data was extracted, false otherwise
     *
     * @note The caller is responsible for byte order conversion (e.g., ntohs, ntohl) on extracted data
     * @note To extract only OUI: pass nullptr for vendor_data_out and 0 for vendor_data_size
     * @note To extract only vendor data: pass nullptr for oui_out
     */
    static bool safe_read_vendor_tlv_data(ieee1905_1::CmduMessageRx &cmdu_rx, void *vendor_data_out,
                                          size_t vendor_data_size, size_t vendor_data_offset = 0,
                                          uint32_t *oui_out = nullptr);

private:
    // Map an IEEE 1905.1 message type to a index [0..msgTypeCount-1]; returns (size_t)-1 if unknown.
    static std::size_t to_index(ieee1905_1::eMessageType t);

    // Per-message-type handler lists (fixed-size array).
    static std::array<std::vector<tlv_function_t>, msgTypeCount> s_handlers;

    // Per-message-type parser lists (fixed-size array).
    static std::array<std::vector<tlv_parser_function_t>, msgTypeCount> s_parsers;

    // Synchronization for registration (one-time initialization).
    static std::mutex s_mu;
};

} // namespace multi_vendor
