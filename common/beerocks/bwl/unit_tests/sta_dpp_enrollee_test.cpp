/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2025 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include <bwl/sta_dpp_enrollee.h>
#include <cstdint>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace bwl::sta_dpp;

namespace {

const char *const kKey = "30770201010420aabbccddeeff00112233445566778899aabbccddeeff0011223344556677";
const char *const kUri = "DPP:C:124/149;K:MDkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDIgAC;;";

class MockBus : public EndpointDppBus {
public:
    // Bootstrap state
    bool call_ok = true;
    std::string uri_to_return{kUri};
    int calls             = 0;
    std::string got_curve;
    std::string got_key;
    std::string got_chan_list{"unset"};
    // pWHM updates the entry with the returned URI on each call (new ID per call).
    std::vector<uint32_t> ids_per_call;
    bool qr_ok = true;
    std::vector<sGeneratedQr> qrs;
    std::vector<std::string> sequence;
    bool read_dpp_intent(bool &e, std::string &r) override
    {
        e = true;
        r = "Enrollee";
        return true;
    }
    bool call_bootstrap_gen(const std::string &curve, const std::string &chan_list,
                            const std::string &key, std::string &uri) override
    {
        sequence.push_back("call");
        got_curve     = curve;
        got_chan_list = chan_list;
        got_key       = key;
        if (!call_ok) {
            return false;
        }
        if (calls < static_cast<int>(ids_per_call.size())) {
            bool updated = false;
            for (auto &entry : qrs) {
                if (entry.uri == uri_to_return) {
                    entry.id = ids_per_call[calls];
                    updated  = true;
                }
            }
            if (!updated) {
                sGeneratedQr entry;
                entry.id  = ids_per_call[calls];
                entry.uri = uri_to_return;
                qrs.push_back(entry);
            }
        }
        calls++;
        uri = uri_to_return;
        return true;
    }
    bool read_generated_qr(std::vector<sGeneratedQr> &entries) override
    {
        sequence.push_back("read_qr");
        entries = qrs;
        return qr_ok;
    }
};

sGeneratedQr qr(uint32_t id, const std::string &uri)
{
    sGeneratedQr e;
    e.id  = id;
    e.uri = uri;
    return e;
}

} // namespace

// ---- Bootstrap registration ----

TEST(StaDppBootstrap, SuccessWithChannelList)
{
    MockBus bus;
    bus.ids_per_call = {5};
    sBootstrapResult res;
    ASSERT_TRUE(register_bootstrap(bus, kKey, "115/36,40,124/149", res));
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.id, 5u);
    EXPECT_EQ(res.uri, kUri);
    EXPECT_EQ(bus.got_curve, "P-256");
    EXPECT_EQ(bus.got_chan_list, "115/36,40,124/149");
    EXPECT_EQ(bus.got_key, kKey);
    EXPECT_EQ(bus.sequence, std::vector<std::string>({"call", "read_qr"}));
}

TEST(StaDppBootstrap, NoListSendsEmptyList)
{
    MockBus bus;
    bus.ids_per_call = {1};
    sBootstrapResult res;
    EXPECT_TRUE(register_bootstrap(bus, kKey, "", res));
    EXPECT_EQ(bus.got_chan_list, "");
}

TEST(StaDppBootstrap, RepeatCallsReadIdAfterEachCall)
{
    MockBus bus;
    bus.qrs.push_back(qr(3, kUri)); // stale entry from before
    bus.qrs.push_back(qr(4, "DPP:K:other;;"));
    bus.ids_per_call = {7, 9};
    sBootstrapResult res;
    ASSERT_TRUE(register_bootstrap(bus, kKey, "", res));
    EXPECT_EQ(res.id, 7u);
    ASSERT_TRUE(register_bootstrap(bus, kKey, "", res));
    EXPECT_EQ(res.id, 9u);
    EXPECT_EQ(bus.sequence,
              std::vector<std::string>({"call", "read_qr", "call", "read_qr"}));
}

static void expect_failed(const sBootstrapResult &res)
{
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.id, 0u);
    EXPECT_TRUE(res.uri.empty());
    EXPECT_FALSE(res.error.empty());
    EXPECT_EQ(res.error.find(kKey), std::string::npos);
}

TEST(StaDppBootstrap, EmptyKeyFailsWithoutCall)
{
    MockBus bus;
    sBootstrapResult res;
    EXPECT_FALSE(register_bootstrap(bus, "", "", res));
    expect_failed(res);
    EXPECT_EQ(bus.calls, 0);
    EXPECT_TRUE(bus.sequence.empty());
}

TEST(StaDppBootstrap, CallErrorFailsWithoutRead)
{
    MockBus bus;
    bus.call_ok = false;
    sBootstrapResult res;
    EXPECT_FALSE(register_bootstrap(bus, kKey, "", res));
    expect_failed(res);
    EXPECT_EQ(bus.sequence, std::vector<std::string>({"call"}));
}

TEST(StaDppBootstrap, BadUriFails)
{
    const std::vector<std::string> bad{"", "dpp:K:x;;", "K:x;;", "DPP"};
    for (const auto &uri : bad) {
        MockBus bus;
        bus.uri_to_return = uri;
        bus.ids_per_call  = {5};
        sBootstrapResult res;
        EXPECT_FALSE(register_bootstrap(bus, kKey, "", res)) << uri;
        expect_failed(res);
    }
}

TEST(StaDppBootstrap, GeneratedQrReadErrorFails)
{
    MockBus bus;
    bus.ids_per_call = {5};
    bus.qr_ok        = false;
    sBootstrapResult res;
    EXPECT_FALSE(register_bootstrap(bus, kKey, "", res));
    expect_failed(res);
}

TEST(StaDppBootstrap, MissingExactMatchFails)
{
    MockBus bus; // call returns URI but GeneratedQR is not updated
    bus.qrs.push_back(qr(4, "DPP:K:other;;"));
    bus.qrs.push_back(qr(6, std::string(kUri) + "x"));
    sBootstrapResult res;
    EXPECT_FALSE(register_bootstrap(bus, kKey, "", res));
    expect_failed(res);
}

TEST(StaDppBootstrap, ZeroIdFails)
{
    MockBus bus;
    bus.ids_per_call = {0};
    sBootstrapResult res;
    EXPECT_FALSE(register_bootstrap(bus, kKey, "", res));
    expect_failed(res);
}

TEST(StaDppBootstrap, DuplicateUriEntries)
{
    MockBus same;
    same.qrs.push_back(qr(8, kUri));
    same.qrs.push_back(qr(8, kUri));
    sBootstrapResult res;
    EXPECT_TRUE(register_bootstrap(same, kKey, "", res));
    EXPECT_EQ(res.id, 8u);

    MockBus diff;
    diff.qrs.push_back(qr(8, kUri));
    diff.qrs.push_back(qr(9, kUri));
    EXPECT_FALSE(register_bootstrap(diff, kKey, "", res));
    expect_failed(res);
}

TEST(StaDppBootstrap, FailureClearsPreviousResult)
{
    MockBus bus;
    bus.ids_per_call = {5};
    sBootstrapResult res;
    ASSERT_TRUE(register_bootstrap(bus, kKey, "", res));
    bus.call_ok = false;
    EXPECT_FALSE(register_bootstrap(bus, kKey, "", res));
    expect_failed(res);
}

// ---- Validation and attachment lifecycle ----

TEST(StaDppBootstrap, ReattachmentsReuseRegisteredId)
{
    MockBus bus;
    bus.ids_per_call = {5, 6, 7};
    sBootstrapRegistration cache;
    cache.observe_attachment(1, "124/149");
    sBootstrapResult res;
    ASSERT_TRUE(register_bootstrap(bus, kKey, "124/149", res));
    cache.id                  = res.id;
    cache.uri                 = res.uri;
    cache.validation_required = false;
    for (uint64_t generation = 2; generation < 20; ++generation) {
        EXPECT_FALSE(cache.observe_attachment(generation, "124/149"));
        EXPECT_TRUE(cache.validation_required);
        EXPECT_EQ(cache.id, 5u);
        EXPECT_EQ(cache.uri, kUri);
        std::string reason;
        EXPECT_EQ(validate_bootstrap(bus, cache.id, cache.uri, reason),
                  eBootstrapValidation::VALID);
        EXPECT_TRUE(reason.empty());
        cache.validation_required = false;
    }
    EXPECT_EQ(bus.calls, 1);
    EXPECT_EQ(bus.got_key, kKey);
    EXPECT_EQ(bus.qrs.size(), 1u);
}

TEST(StaDppBootstrap, ChannelPolicyChangeInvalidatesButSameAttachmentDoesNot)
{
    sBootstrapRegistration cache;
    cache.observe_attachment(1, "124/149");
    cache.id                  = 5;
    cache.uri                 = kUri;
    cache.validation_required = false;
    EXPECT_FALSE(cache.observe_attachment(1, "124/149"));
    EXPECT_FALSE(cache.validation_required);
    EXPECT_EQ(cache.id, 5u);
    EXPECT_TRUE(cache.observe_attachment(2, "115/36,40"));
    EXPECT_TRUE(cache.validation_required);
    EXPECT_EQ(cache.id, 0u);
    EXPECT_TRUE(cache.uri.empty());
    EXPECT_EQ(cache.chan_list, "115/36,40");
}

TEST(StaDppBootstrap, SupplicantRestartAllowsOneReplacement)
{
    MockBus bus;
    bus.ids_per_call = {5, 6};
    sBootstrapResult res;
    ASSERT_TRUE(register_bootstrap(bus, kKey, "124/149", res));
    bus.qrs[0].id = 0; // pWHM reset on wpa_supplicant start; row and URI are kept.
    std::string reason;
    EXPECT_EQ(validate_bootstrap(bus, res.id, res.uri, reason), eBootstrapValidation::INVALID);
    EXPECT_EQ(reason, "id-reset");
    ASSERT_TRUE(register_bootstrap(bus, kKey, "124/149", res));
    EXPECT_EQ(res.id, 6u);
    EXPECT_EQ(validate_bootstrap(bus, res.id, res.uri, reason), eBootstrapValidation::VALID);
    EXPECT_EQ(bus.calls, 2);
    EXPECT_EQ(bus.got_key, kKey);
    EXPECT_EQ(bus.qrs.size(), 1u);
}

TEST(StaDppBootstrap, TransientValidationFailureDoesNotGenerate)
{
    MockBus bus;
    bus.ids_per_call = {5, 6};
    sBootstrapResult res;
    ASSERT_TRUE(register_bootstrap(bus, kKey, "124/149", res));
    std::string reason;
    bus.qr_ok = false;
    EXPECT_EQ(validate_bootstrap(bus, res.id, res.uri, reason), eBootstrapValidation::UNAVAILABLE);
    EXPECT_EQ(reason, "metadata-unavailable");
    bus.qr_ok = true;
    EXPECT_EQ(validate_bootstrap(bus, res.id, res.uri, reason), eBootstrapValidation::VALID);
    EXPECT_EQ(bus.calls, 1);
}

TEST(StaDppBootstrap, ResetMissingOrChangedMetadataConfirmsInvalidation)
{
    const std::vector<std::vector<sGeneratedQr>> snapshots = {
        {}, {qr(0, kUri)}, {qr(8, kUri)}, {qr(5, "DPP:K:other;;")}};
    const std::vector<std::string> reasons = {"row-missing", "id-reset", "id-changed",
                                              "row-missing"};
    for (size_t i = 0; i < snapshots.size(); ++i) {
        MockBus bus;
        bus.qrs = snapshots[i];
        std::string reason;
        EXPECT_EQ(validate_bootstrap(bus, 5, kUri, reason), eBootstrapValidation::INVALID);
        EXPECT_EQ(reason, reasons[i]);
        EXPECT_EQ(bus.calls, 0);
    }
}

TEST(StaDppBootstrap, AmbiguousMetadataIsNotProofOfLoss)
{
    MockBus bus;
    bus.qrs = {qr(5, kUri), qr(6, kUri)};
    std::string reason;
    EXPECT_EQ(validate_bootstrap(bus, 5, kUri, reason), eBootstrapValidation::UNAVAILABLE);
    EXPECT_EQ(reason, "metadata-ambiguous");
    EXPECT_EQ(bus.calls, 0);
}

TEST(StaDppBootstrap, RegistrationValidationIsEndpointLocal)
{
    MockBus first, second;
    first.qrs  = {qr(5, kUri)};
    second.qrs = {qr(5, "DPP:K:another-endpoint;;")};
    std::string reason;
    EXPECT_EQ(validate_bootstrap(first, 5, kUri, reason), eBootstrapValidation::VALID);
    EXPECT_EQ(validate_bootstrap(second, 5, kUri, reason), eBootstrapValidation::INVALID);
    EXPECT_EQ(first.calls + second.calls, 0);
}

TEST(StaDppBootstrap, InvalidCacheDoesNotQueryOrGenerate)
{
    MockBus bus;
    std::string reason;
    EXPECT_EQ(validate_bootstrap(bus, 0, kUri, reason), eBootstrapValidation::INVALID);
    EXPECT_EQ(validate_bootstrap(bus, 5, "", reason), eBootstrapValidation::INVALID);
    EXPECT_EQ(validate_bootstrap(bus, 5, "not-a-DPP-URI", reason), eBootstrapValidation::INVALID);
    EXPECT_TRUE(bus.sequence.empty());
    EXPECT_EQ(bus.calls, 0);
}

TEST(StaDppEnrolleeEvent, ChirpSentIsChirpTx)
{
    EXPECT_EQ(parse_enrollee_event("<3>DPP-TX dst=ff:ff:ff:ff:ff:ff freq=5180 type=13"),
              eEnrolleeEvent::CHIRP_TX);
    EXPECT_EQ(parse_enrollee_event("DPP-TX dst=ff:ff:ff:ff:ff:ff freq=2437 type=13"),
              eEnrolleeEvent::CHIRP_TX);
}

TEST(StaDppEnrolleeEvent, OtherTxFramesAreIgnored)
{
    // Auth Response, Reconfig Announcement, unknown type and TX status are no chirp progress.
    EXPECT_EQ(parse_enrollee_event("<3>DPP-TX dst=02:00:00:00:00:01 freq=5180 type=1"),
              eEnrolleeEvent::NONE);
    EXPECT_EQ(parse_enrollee_event("<3>DPP-TX dst=ff:ff:ff:ff:ff:ff freq=5180 type=14"),
              eEnrolleeEvent::NONE);
    EXPECT_EQ(parse_enrollee_event("<3>DPP-TX dst=ff:ff:ff:ff:ff:ff freq=5180 type=130"),
              eEnrolleeEvent::NONE);
    EXPECT_EQ(
        parse_enrollee_event("<3>DPP-TX-STATUS dst=ff:ff:ff:ff:ff:ff freq=5180 result=FAILED"),
        eEnrolleeEvent::NONE);
    EXPECT_EQ(parse_enrollee_event("<3>DPP-TX"), eEnrolleeEvent::NONE);
}

TEST(StaDppEnrolleeEvent, ChirpTxReportsFrequency)
{
    uint32_t freq = 1;
    EXPECT_EQ(parse_enrollee_event("<3>DPP-TX dst=ff:ff:ff:ff:ff:ff freq=5180 type=13", &freq),
              eEnrolleeEvent::CHIRP_TX);
    EXPECT_EQ(freq, 5180u);
    // The token order does not matter.
    EXPECT_EQ(parse_enrollee_event("DPP-TX type=13 dst=ff:ff:ff:ff:ff:ff freq=2437", &freq),
              eEnrolleeEvent::CHIRP_TX);
    EXPECT_EQ(freq, 2437u);
}

TEST(StaDppEnrolleeEvent, ChirpTxWithoutValidFrequencyReportsZero)
{
    const std::vector<std::string> lines = {
        "<3>DPP-TX dst=ff:ff:ff:ff:ff:ff type=13",
        "<3>DPP-TX dst=ff:ff:ff:ff:ff:ff freq= type=13",
        "<3>DPP-TX dst=ff:ff:ff:ff:ff:ff freq=abc type=13",
        "<3>DPP-TX dst=ff:ff:ff:ff:ff:ff freq=51800x type=13",
        "<3>DPP-TX dst=ff:ff:ff:ff:ff:ff freq=-5180 type=13",
        "<3>DPP-TX dst=ff:ff:ff:ff:ff:ff freq=518000 type=13",
    };
    for (const auto &line : lines) {
        uint32_t freq = 1;
        EXPECT_EQ(parse_enrollee_event(line, &freq), eEnrolleeEvent::CHIRP_TX) << line;
        EXPECT_EQ(freq, 0u) << line;
    }
}

TEST(StaDppEnrolleeEvent, OtherEventsReportNoFrequency)
{
    uint32_t freq = 1;
    EXPECT_EQ(parse_enrollee_event("<3>DPP-TX dst=02:00:00:00:00:01 freq=5180 type=1", &freq),
              eEnrolleeEvent::NONE);
    EXPECT_EQ(freq, 0u);
    freq = 1;
    EXPECT_EQ(parse_enrollee_event("<3>DPP-RX src=02:00:00:00:00:01 freq=5180 type=0", &freq),
              eEnrolleeEvent::AUTH_REQUEST_RX);
    EXPECT_EQ(freq, 0u);
}

TEST(StaDppEnrolleeEvent, ExistingEventsUnchanged)
{
    EXPECT_EQ(parse_enrollee_event("<3>DPP-RX src=02:00:00:00:00:01 freq=5180 type=0"),
              eEnrolleeEvent::AUTH_REQUEST_RX);
    EXPECT_EQ(
        parse_enrollee_event("<3>DPP-RX src=02:00:00:00:00:01 freq=5180 type=0 ignore=no-match"),
        eEnrolleeEvent::NONE);
    EXPECT_EQ(parse_enrollee_event("<3>DPP-RX src=02:00:00:00:00:01 freq=5180 type=13"),
              eEnrolleeEvent::NONE);
    EXPECT_EQ(parse_enrollee_event("<3>DPP-CHIRP-STOPPED"), eEnrolleeEvent::CHIRP_STOPPED);
    EXPECT_EQ(parse_enrollee_event("<3>DPP-AUTH-SUCCESS init=0"), eEnrolleeEvent::AUTH_SUCCESS);
    EXPECT_EQ(parse_enrollee_event("<3>DPP-CONF-RECEIVED"), eEnrolleeEvent::CONF_RECEIVED);
    EXPECT_EQ(parse_enrollee_event("<3>DPP-CONF-FAILED"), eEnrolleeEvent::FAILED);
    EXPECT_EQ(parse_enrollee_event("<3>DPP-FAIL Configurator rejected"), eEnrolleeEvent::FAILED);
    EXPECT_EQ(parse_enrollee_event(""), eEnrolleeEvent::NONE);
    EXPECT_EQ(parse_enrollee_event("<3"), eEnrolleeEvent::NONE);
}

TEST(StaDppEnrolleeEvent, IgnoredAuthRequestIsNotFailure)
{
    // wpas_dpp_rx_auth_req() reports DPP-RX type=0 first, then drops a request for another key.
    const std::string own_key =
        "<3>DPP-FAIL No matching own bootstrapping key found - ignore message";
    EXPECT_EQ(parse_enrollee_event(own_key), eEnrolleeEvent::AUTH_REQUEST_IGNORED);
    EXPECT_EQ(
        parse_enrollee_event("DPP-FAIL Mismatching peer PKEX bootstrapping key - ignore message"),
        eEnrolleeEvent::AUTH_REQUEST_IGNORED);
    // A second Auth Request during a running exchange is dropped; that exchange goes on.
    const std::string busy = "<3>DPP-FAIL Already in DPP authentication exchange - ignore new one";
    EXPECT_EQ(parse_enrollee_event(busy), eEnrolleeEvent::NONE);
    EXPECT_EQ(parse_enrollee_event("<3>DPP-FAIL No Auth Confirm received"), eEnrolleeEvent::FAILED);
    EXPECT_EQ(parse_enrollee_event("<3>DPP-FAIL"), eEnrolleeEvent::FAILED);
}

namespace {

bwl::sScanResult entry_with_stale_cce()
{
    bwl::sScanResult entry;
    entry.dpp_cce_known   = true;
    entry.dpp_cce_present = true;
    entry.seen_ms_ago     = 5;
    return entry;
}

class MockScanBus : public CceScanBus {
public:
    bool read_ok     = true;
    bool in_progress = false;
    bool start_ok    = true;
    int reads        = 0;
    std::vector<std::string> starts;
    bool read_scan_in_progress(bool &value) override
    {
        reads++;
        value = in_progress;
        return read_ok;
    }
    bool start_scan(const std::string &reason) override
    {
        starts.push_back(reason);
        return start_ok;
    }
};

} // namespace

TEST(StaDppCceScanKeys, MissingKeysAreUnknown)
{
    auto entry = entry_with_stale_cce();
    apply_cce_scan_keys(sCceScanKeys(), entry);
    EXPECT_FALSE(entry.dpp_cce_known);
    EXPECT_FALSE(entry.dpp_cce_present);
    EXPECT_EQ(entry.seen_ms_ago, -1);
    EXPECT_FALSE(bwl::sScanResult().dpp_cce_known);
    EXPECT_EQ(bwl::sScanResult().seen_ms_ago, -1);
}

TEST(StaDppCceScanKeys, KnownEntryReportsPresenceAndAge)
{
    sCceScanKeys keys;
    keys.has_cce       = true;
    keys.cce           = true;
    keys.has_cce_known = true;
    keys.cce_known     = true;
    keys.has_seen      = true;
    keys.seen_ms_ago   = 1500;
    bwl::sScanResult entry;
    apply_cce_scan_keys(keys, entry);
    EXPECT_TRUE(entry.dpp_cce_known);
    EXPECT_TRUE(entry.dpp_cce_present);
    EXPECT_EQ(entry.seen_ms_ago, 1500);

    keys.cce = false;
    apply_cce_scan_keys(keys, entry);
    EXPECT_TRUE(entry.dpp_cce_known);
    EXPECT_FALSE(entry.dpp_cce_present);
}

TEST(StaDppCceScanKeys, PresenceWithoutKnownIsUnknown)
{
    sCceScanKeys keys;
    keys.has_cce = true;
    keys.cce     = true;
    auto entry   = entry_with_stale_cce();
    apply_cce_scan_keys(keys, entry); // DPPCCEKnown missing
    EXPECT_FALSE(entry.dpp_cce_known);
    EXPECT_FALSE(entry.dpp_cce_present);

    keys.has_cce_known = true; // DPPCCEKnown false: no element buffer was parsed
    apply_cce_scan_keys(keys, entry);
    EXPECT_FALSE(entry.dpp_cce_known);
    EXPECT_FALSE(entry.dpp_cce_present);

    keys.cce_known = true;
    keys.has_cce   = false; // DPPCCE missing or unreadable
    apply_cce_scan_keys(keys, entry);
    EXPECT_FALSE(entry.dpp_cce_known);
    EXPECT_FALSE(entry.dpp_cce_present);
}

TEST(StaDppCceScanKeys, InvalidAgeIsUnknown)
{
    sCceScanKeys keys;
    keys.seen_ms_ago = 100; // value without a readable key
    bwl::sScanResult entry;
    apply_cce_scan_keys(keys, entry);
    EXPECT_EQ(entry.seen_ms_ago, -1);

    keys.has_seen    = true;
    keys.seen_ms_ago = -7;
    apply_cce_scan_keys(keys, entry);
    EXPECT_EQ(entry.seen_ms_ago, -1);

    keys.seen_ms_ago = 0;
    apply_cce_scan_keys(keys, entry);
    EXPECT_EQ(entry.seen_ms_ago, 0);
    keys.seen_ms_ago = int64_t(1) << 40;
    apply_cce_scan_keys(keys, entry);
    EXPECT_EQ(entry.seen_ms_ago, INT32_MAX);
}

TEST(StaDppCceScanRequest, StartsWhenNoScanRuns)
{
    MockScanBus bus;
    EXPECT_EQ(request_cce_scan(bus, false), eCceScanRequest::STARTED);
    EXPECT_EQ(bus.reads, 1);
    ASSERT_EQ(bus.starts.size(), 1U);
    EXPECT_EQ(bus.starts[0], "DppCceWatch");
}

TEST(StaDppCceScanRequest, RefusesWhileHalScanActive)
{
    MockScanBus bus;
    EXPECT_EQ(request_cce_scan(bus, true), eCceScanRequest::HAL_SCAN_ACTIVE);
    EXPECT_EQ(bus.reads, 0);
    EXPECT_TRUE(bus.starts.empty());
}

TEST(StaDppCceScanRequest, RefusesWhilePlatformScanRuns)
{
    MockScanBus bus;
    bus.in_progress = true;
    EXPECT_EQ(request_cce_scan(bus, false), eCceScanRequest::PWHM_SCAN_ACTIVE);
    EXPECT_TRUE(bus.starts.empty());
}

TEST(StaDppCceScanRequest, RefusesWhenReadinessUnknown)
{
    MockScanBus bus;
    bus.read_ok = false;
    EXPECT_EQ(request_cce_scan(bus, false), eCceScanRequest::READINESS_UNKNOWN);
    EXPECT_TRUE(bus.starts.empty());
}

TEST(StaDppCceScanRequest, ReportsStartFailure)
{
    MockScanBus bus;
    bus.start_ok = false;
    EXPECT_EQ(request_cce_scan(bus, false), eCceScanRequest::START_FAILED);
    EXPECT_EQ(bus.starts.size(), 1U);
    EXPECT_STREQ(cce_scan_request_name(eCceScanRequest::START_FAILED), "start-failed");
}
