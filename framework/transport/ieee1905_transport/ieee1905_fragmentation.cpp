/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "ieee1905_fragmentation.h"

#include <mapf/common/logger.h>

#include <algorithm>

namespace beerocks {
namespace transport {

// When fragmenting a message it is expected that only the last fragment will have an END OF MESSAGE TLV.
// However, when sending a TLV smaller than then the minimum ethernet frame, a padding is added to the frame.
// This padding might cause us having unwanted EOM TLV.
static const int kIeee1905FragmentationMinimum = 64 - 2 * 6 /* src, dest MAC */ - 2 /* Type */;

// De-fragmentation (reassembly) of received packets will be done before publishing a CMDU to the broker (defragmentation is
// not required for tunneling and relayed packets).
//
// De-fragmentation could fail if one or more fragments are never received - thus the fragments of a CMDU should be
// collected and buffered until either the complete CMDU is available or a certain timeout has elapsed
// (IEEE1905 does not specify the duration of this timeout).
//
// The current implementation does not support out-of-order arrival of fragments.
//
// see paragraph 7.1.2 of IEEE1905.1-2013
bool Ieee1905Defragmenter::defragment(const sMacAddr &src, uint8_t *&cmdu, size_t &length)
{
    Ieee1905CmduHeader *ch = reinterpret_cast<Ieee1905CmduHeader *>(cmdu);
    if (ch->fragmentId == 0 && ch->GetLastFragmentIndicator() == 1) {
        return true; // not a fragmented CMDU.
    }

    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    // de-fragmentation map cleanup - go over all entries and remove old ones or complete ones
    for (auto it = de_fragmentation_map_.begin(); it != de_fragmentation_map_.end();) {
        auto &val = it->second;

        if (val.complete) {
            it = de_fragmentation_map_.erase(it);
        } else if (now > val.time + kMaximumDeFragmentationAge) {
            // Note: this is not necessarily related to the current fragment referenced by the argument to this method
            MAPF_DBG("defragmentation timeout - dropping a packet from defragmentation buffer");
            it = de_fragmentation_map_.erase(it);
            if (m_on_timeout) {
                m_on_timeout();
            }
        } else {
            ++it;
        }
    }

    // build the key to the de-fragmentation map
    DeFragmentationKey key;
    key.src         = src;
    key.messageType = ch->messageType;
    key.messageId   = ch->messageId;

    auto &val = de_fragmentation_map_[key]; // find a match (or create a new entry)

    // limit the map size (to prevent a possible memory exhaustion attack)
    // Note: at this stage there could be one extra entry in the map (this is still effective and the code is simple)
    if (int(de_fragmentation_map_.size()) > kMaximumDeFragmentationThreads) {
        de_fragmentation_map_.erase(key);
        MAPF_WARN("too many de-fragmentation threads - dropping packet");
        return false;
    }

    val.time = now;

    // only accept fragments in order - check if this is the expected fragment
    if (val.numFragments != ch->fragmentId) {
        MAPF_DBG("dropping an out-of-order fragment");
        return false;
    }

    // copy the IEEE1905 header from the first fragment
    if (ch->fragmentId == 0) {
        std::copy_n(cmdu, sizeof(Ieee1905CmduHeader), val.buf);
        val.bufIndex = sizeof(Ieee1905CmduHeader);

        // set the last fragment indicator flag as this will be the header of a complete CMDU
        Ieee1905CmduHeader *hdr = reinterpret_cast<Ieee1905CmduHeader *>(val.buf);
        hdr->SetLastFragmentIndicator(1);
    }

    // concat the fragment body (excluding the IEEE1905 header)
    size_t fragmentTlvsLength = length - sizeof(Ieee1905CmduHeader);
    if (val.bufIndex + fragmentTlvsLength >= kMaximumDeFragmentionSize) {
        MAPF_WARN("defragmentation buffer overflow - dropping fragment");
        return false;
    }

    // Only count end of message TLV for the last fragment
    if (ch->GetLastFragmentIndicator() == 0) {
        uint8_t *body = cmdu + sizeof(Ieee1905CmduHeader);

        // Complete a TLV header that was split across the previous fragment boundary.
        size_t headerBytes = 0;
        if (val.partialTlvHeaderLength > 0) {
            headerBytes = std::min(sizeof(Tlv) - val.partialTlvHeaderLength, fragmentTlvsLength);
            std::copy_n(body, headerBytes, val.partialTlvHeader + val.partialTlvHeaderLength);
            val.partialTlvHeaderLength += headerBytes;
            if (val.partialTlvHeaderLength == sizeof(Tlv)) {
                // Header complete - the TLV value continues in this fragment.
                val.splitTlvRemaining =
                    reinterpret_cast<Tlv *>(val.partialTlvHeader)->size() - sizeof(Tlv);
                val.partialTlvHeaderLength = 0;
            }
        }
        size_t bodyLength = fragmentTlvsLength - headerBytes;

        if (val.partialTlvHeaderLength > 0) {
            // The whole fragment was consumed by the (still incomplete) TLV header.
        } else if (val.splitTlvRemaining >= bodyLength) {
            // The whole fragment is the continuation of a TLV split on a byte boundary.
            val.splitTlvRemaining -= bodyLength;
        } else {
            // Skip leading bytes that continue a split TLV, then walk whole TLVs for EOM/padding.
            size_t tlvsLength = bodyLength - val.splitTlvRemaining;
            Tlv *tlv          = reinterpret_cast<Tlv *>(body + headerBytes + val.splitTlvRemaining);
            val.splitTlvRemaining = 0;

            // Never read a TLV field beyond the end of this fragment (tlvsLength bytes left).
            while (tlvsLength > 0 && tlv->type() != 0) {
                if (tlvsLength < sizeof(Tlv)) {
                    // Only part of the TLV header fits; the rest is in the next fragment.
                    std::copy_n(reinterpret_cast<uint8_t *>(tlv), tlvsLength, val.partialTlvHeader);
                    val.partialTlvHeaderLength = tlvsLength;
                    tlvsLength                 = 0;
                    break;
                }
                if (tlv->size() > tlvsLength) {
                    // TLV continues in the next fragment (byte-boundary split).
                    val.splitTlvRemaining = tlv->size() - tlvsLength;
                    tlvsLength            = 0;
                    break;
                }
                tlvsLength -= tlv->size();
                tlv = tlv->next();
            }
            // Any remaining bytes start with an EOM TLV (type 0) - drop it and its padding.
            fragmentTlvsLength -= tlvsLength;
        }
    }

    std::copy_n(cmdu + sizeof(Ieee1905CmduHeader), fragmentTlvsLength, val.buf + val.bufIndex);
    val.bufIndex += fragmentTlvsLength;
    val.numFragments++;

    // detect defragmentation completion and update the CMDU
    if (ch->GetLastFragmentIndicator()) {
        val.complete =
            true;         // mark as complete - the map entry will be deleted on the next invocation
        cmdu   = val.buf; // buffer is valid until next invocation of this method
        length = val.bufIndex;

        return true;
    }

    MAPF_DBG("buffering a fragment of an incomplete CMDU");
    return false;
}

// When an IEEE1905 packet (CMDU) is larger than a standard defined threshold (1500 bytes) it should be
// fragmented into smaller than 1500 bytes fragments.
//
// Fragmentation is normally done on TLV boundaries (see paragraph 7.1.1 of IEEE1905.1-2013).
// A single TLV larger than the fragmentation threshold is split on a byte boundary across
// multiple fragments; reassembly concatenates fragment bodies in Ieee1905Defragmenter::defragment().
// The CMDU length is considered starting from the IEEE1905 header (it does not include the Ethernet header)
//
// see paragraph 7.1.1 of IEEE1905.1-2013

// Minimum body (bytes after the IEEE1905 CMDU header) of any transmitted fragment.
// verify_packet() on the receiver drops IEEE1905 packets shorter than header + 3 bytes,
// so a byte-split continuation fragment must never carry only 1 or 2 body bytes.
static constexpr int kMinFragmentBodyLength = 3;

// Choose how many bytes of an oversized TLV (with `remaining` bytes still to send) go into
// the current fragment, given the per-fragment body limit `threshold`.
//
// Normally this is min(remaining, threshold). If that would leave a tail of only 1 or 2
// bytes for the next fragment, the current chunk is reduced so that exactly
// kMinFragmentBodyLength bytes remain (Option B rebalancing). Whole-TLV boundaries of
// TLVs that fit in a fragment are not affected by this helper.
static int balanced_split_chunk_length(int remaining, int threshold)
{
    if (remaining <= threshold) {
        return remaining;
    }
    int chunk = threshold;
    int tail  = remaining - chunk;
    if (tail < kMinFragmentBodyLength) {
        chunk -= kMinFragmentBodyLength - tail;
    }
    return chunk;
}

bool ieee1905_fragment_cmdu(
    const uint8_t *cmdu, size_t length,
    const std::function<bool(uint8_t *fragment, size_t fragment_length)> &send_fragment)
{
    uint8_t buf[kIeee1905FragmentationThreashold + sizeof(Ieee1905CmduHeader)] = {
        0}; // fragment size + TLV end of message size

    // copy the IEEE1905 header (will be reused by all fragments)
    std::copy_n(cmdu, sizeof(Ieee1905CmduHeader), buf);

    uint8_t *payload          = const_cast<uint8_t *>(cmdu) + sizeof(Ieee1905CmduHeader);
    int remainingPacketLength = int(length) - int(sizeof(Ieee1905CmduHeader));
    int payloadOffset         = 0;

    // Bytes of an oversized TLV still to send as raw continuation in following fragment(s).
    int splitTlvRemaining = 0;

    for (int fragmentId = 0; fragmentId < 256 && remainingPacketLength > 0; fragmentId++) {
        // search for the optimal TLV boundary for fragmentation
        // include as many TLVs as can fit into the maximum fragment size (threashold).
        int fragmentTlvsLength = 0;

        if (splitTlvRemaining > 0) {
            // Continue an oversized TLV; rebalance so the next continuation is >= 3 bytes.
            fragmentTlvsLength =
                balanced_split_chunk_length(splitTlvRemaining, kIeee1905FragmentationThreashold);
            splitTlvRemaining -= fragmentTlvsLength;
        } else {
            // Pack as many whole TLVs as fit within the fragmentation threshold.
            Tlv *nextTlv = reinterpret_cast<Tlv *>(payload + payloadOffset);
            while (true) {
                int avail = remainingPacketLength - fragmentTlvsLength;
                if (avail == 0) {
                    break;
                }
                if (avail < int(sizeof(Tlv))) {
                    MAPF_WARN("bad packet format - extra bytes at end of message.");
                    return false;
                }
                if (nextTlv->type() == 0 && avail > int(nextTlv->size())) {
                    MAPF_WARN("bad packet format - extra bytes after end of message TLV. "
                              << avail);
                    return false;
                }
                if (int(nextTlv->size()) > avail) {
                    MAPF_WARN("bad packet format - TLV exceeds packet bounds.");
                    return false;
                }
                if (fragmentTlvsLength == 0 &&
                    int(nextTlv->size()) > kIeee1905FragmentationThreashold) {
                    // TLV too large for one fragment on a TLV boundary — byte-split it,
                    // rebalancing so the continuation fragment carries at least 3 bytes.
                    fragmentTlvsLength = balanced_split_chunk_length(
                        int(nextTlv->size()), kIeee1905FragmentationThreashold);
                    splitTlvRemaining = int(nextTlv->size()) - fragmentTlvsLength;
                    break;
                }
                if (fragmentTlvsLength + int(nextTlv->size()) > kIeee1905FragmentationThreashold) {
                    break;
                }
                fragmentTlvsLength += int(nextTlv->size());
                nextTlv = nextTlv->next();
            }
        }

        if (fragmentTlvsLength == 0) {
            MAPF_WARN("bad packet format - unable to build a non-empty fragment.");
            return false;
        }

        std::copy_n(payload + payloadOffset, fragmentTlvsLength, buf + sizeof(Ieee1905CmduHeader));
        remainingPacketLength -= fragmentTlvsLength;
        payloadOffset += fragmentTlvsLength;

        // update IEEE1905 header (fragmentId and lastFragmentIndicator)
        Ieee1905CmduHeader *hdr = reinterpret_cast<Ieee1905CmduHeader *>(buf);
        hdr->fragmentId         = fragmentId;
        hdr->SetLastFragmentIndicator(remainingPacketLength == 0 ? 1 : 0);

        size_t fragment_length = sizeof(Ieee1905CmduHeader) + fragmentTlvsLength;

        if (remainingPacketLength > 0 && fragment_length < kIeee1905FragmentationMinimum) {
            MAPF_WARN("Fragment is too small - possibly padded EOM!");
        }

        // send the fragment
        MAPF_DBG("sending fragment " << (int)fragmentId << " (length=" << fragment_length << ").");
        if (!send_fragment(buf, fragment_length)) {
            return false;
        }
    }

    if (remainingPacketLength > 0) {
        MAPF_WARN("bad packet format - packet is too long.");
        return false;
    }

    return true;
}

} // namespace transport
} // namespace beerocks
