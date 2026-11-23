/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#ifndef MAP_TRANSPORT_IEEE1905_FRAGMENTATION_H_
#define MAP_TRANSPORT_IEEE1905_FRAGMENTATION_H_

#include <tlvf/tlvftypes.h>

#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <net/ethernet.h>

namespace beerocks {
namespace transport {

// IEEE1905 CMDU header in packed format
#pragma pack(push, 1)
struct Ieee1905CmduHeader {
    uint8_t messageVersion;
    uint8_t _reservedField0;
    uint16_t messageType;
    uint16_t messageId;
    uint8_t fragmentId;
    uint8_t flags;
    void SetLastFragmentIndicator(bool value) { flags = (flags & ~0x80) | (value ? 0x80 : 0x00); };
    bool GetLastFragmentIndicator() const { return (flags & 0x80); };
    void SetRelayIndicator(bool value) { flags = (flags & ~0x40) | (value ? 0x40 : 0x00); };
    bool GetRelayIndicator() const { return (flags & 0x40); };
};
#pragma pack(pop)

// packed TLV struct to help in the parsing of the packet
#pragma pack(push, 1)
struct Tlv {
protected:
    uint8_t tlvType;
    uint16_t tlvLength;

public:
    uint8_t &type() { return tlvType; }

    Tlv *next() { return (Tlv *)((uint8_t *)this + size()); }

    size_t size() { return sizeof(Tlv) + ntohs(tlvLength); }
};
#pragma pack(pop)

// IEEE1905 packets (CMDU) should be fragmented if larger than this threashold
static constexpr int kIeee1905FragmentationThreashold = tlvf::MAX_TLV_SIZE;

/**
 * @brief Split a CMDU into fragments of at most kIeee1905FragmentationThreashold body bytes.
 *
 * Whole TLVs are packed into a fragment as long as they fit. A single TLV larger than the
 * threshold is split on a byte boundary across multiple fragments.
 *
 * @param[in] cmdu CMDU (IEEE1905 header followed by the TLVs).
 * @param[in] length Length of the CMDU in bytes.
 * @param[in] send_fragment Called for each fragment (IEEE1905 header + body). The buffer is
 * only valid during the call.
 * @return true on success and false if the CMDU is malformed or send_fragment failed.
 */
bool ieee1905_fragment_cmdu(
    const uint8_t *cmdu, size_t length,
    const std::function<bool(uint8_t *fragment, size_t fragment_length)> &send_fragment);

/**
 * @brief Reassembles fragmented CMDUs received from the network.
 *
 * The current implementation does not support out-of-order arrival of fragments.
 */
class Ieee1905Defragmenter {
public:
    /**
     * @param[in] on_timeout Called each time an incomplete CMDU is dropped because not all of
     * its fragments were received in time.
     */
    explicit Ieee1905Defragmenter(std::function<void()> on_timeout = nullptr)
        : m_on_timeout(std::move(on_timeout))
    {
    }

    /**
     * @brief Add a received CMDU (fragment) to the defragmentation buffer.
     *
     * @param[in] src Source MAC address of the CMDU.
     * @param[in,out] cmdu Received CMDU. When the CMDU is complete, it is updated to point to the
     * reassembled CMDU (valid until the next call of this method).
     * @param[in,out] length Length of the CMDU, updated together with cmdu.
     * @return true if cmdu now holds a complete CMDU, false if the fragment was buffered or dropped.
     */
    bool defragment(const sMacAddr &src, uint8_t *&cmdu, size_t &length);

private:
    // two IEE1905 packet fragments received from the same src address and with the same messageId
    // will be considered as belonging to the same PDU iff they are received within a kMaximumDeFragmentionAge duration.
    // This duration should be long enough to allow for any buffered/in-transit fragments to arrive; and
    // should be short enough to handle the case when a device reboots (and reuses the same messageId).
    const std::chrono::milliseconds kMaximumDeFragmentationAge = std::chrono::milliseconds(1000);

    // limit the size of the de-fragmentation map (to prevent memory exhaustion attack)
    const int kMaximumDeFragmentationThreads = 16;

    static const size_t kMaximumDeFragmentionSize = (64 * 1024);

    struct DeFragmentationKey {
        sMacAddr src;
        uint16_t
            messageType; // required to distinguish the case of Request / Reply (when the reply carries an out of sync MID)
        uint16_t messageId;
    };
    struct DeFragmentationKeyCompare {
        // implement Compare for the std::map template
        bool operator()(const DeFragmentationKey &lhs, const DeFragmentationKey &rhs) const
        {
            // compare based on src address first then messageId
            int srccmp = memcmp(lhs.src.oct, rhs.src.oct, ETH_ALEN);
            if (srccmp)
                return srccmp < 0;
            else if (lhs.messageType != rhs.messageType)
                return lhs.messageType < rhs.messageType;
            else
                return lhs.messageId < rhs.messageId;
        }
    };
    struct DeFragmentationValue {
        std::chrono::steady_clock::time_point time;
        uint8_t numFragments                   = 0;
        uint8_t buf[kMaximumDeFragmentionSize] = {0};
        int bufIndex                           = 0;
        bool complete                          = false;
        // Leading bytes of the next fragment that continue a TLV split on a byte boundary.
        size_t splitTlvRemaining = 0;
        // Leading TLV header bytes (type, maybe 1st length byte) at the end of the previous
        // fragment; the rest of the header is at the start of the next fragment.
        uint8_t partialTlvHeader[sizeof(Tlv)] = {0};
        size_t partialTlvHeaderLength         = 0;
    };
    std::map<DeFragmentationKey, DeFragmentationValue, DeFragmentationKeyCompare>
        de_fragmentation_map_;

    std::function<void()> m_on_timeout;
};

} // namespace transport
} // namespace beerocks

#endif // MAP_TRANSPORT_IEEE1905_FRAGMENTATION_H_
