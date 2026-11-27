/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2020 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "coordinated_cac_task.h"
#include "task_pool_interface.h"
#include <bcl/beerocks_defines.h>
#include <bcl/beerocks_utils.h>
#include <bcl/beerocks_wifi_channel.h>
#include <beerocks/tlvf/beerocks_message.h>

namespace beerocks {
namespace coordinated_cac {

constexpr std::chrono::seconds CacFsm::DEFAULT_WAIT_FOR_SWITCH_CHANNEL_REPORT;
constexpr std::chrono::seconds CacFsm::DEFAULT_MAX_WAIT_FOR_CAC_TERMINATION;

/////////////
// cac fsm //
/////////////

std::ostream &operator<<(std::ostream &out, const fsm_state &value)
{
    switch (value) {
    case fsm_state::IDLE:
        out << "IDLE";
        break;
    case fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT:
        out << "WAIT_FOR_SWITCH_CHANNEL_REPORT";
        break;
    case fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT:
        out << "WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT";
        break;
    case fsm_state::WAIT_FOR_CAC_TERMINATION:
        out << "WAIT_FOR_CAC_TERMINATION";
        break;
    case fsm_state::ERROR:
        out << "ERROR";
        break;
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const fsm_event &value)
{
    switch (value) {
    case fsm_event::CAC_REQUEST:
        out << "CAC_REQUEST";
        break;
    case fsm_event::CAC_TERMINATION_REQUEST:
        out << "CAC_TERMINATION_REQUEST";
        break;
    case fsm_event::CAC_TERMINATION_RESPONSE:
        out << "CAC_TERMINATION_RESPONSE";
        break;
    case fsm_event::SWITCH_CHANNEL_REPORT:
        out << "SWITCH_CHANNEL_REPORT";
        break;
    case fsm_event::SWITCH_CHANNEL_DURATION_TIME:
        out << "SWITCH_CHANNEL_DURATION_TIME";
        break;
    case fsm_event::CAC_STARTED_NOTIFICATION:
        out << "CAC_STARTED_NOTIFICATION";
        break;
    case fsm_event::CAC_COMPLETED_NOTIFICATION:
        out << "CAC_COMPLETED_NOTIFICATION";
        break;
    case fsm_event::PERIODIC:
        out << "PERIODIC";
        break;
    }
    return out;
}

CacFsm::CacFsm(TaskPoolInterface &task_pool, ieee1905_1::CmduMessageTx &cmdu_tx, SendCmdu send_cmdu,
               Clock now)
    : beerocks_fsm(fsm_state::IDLE), m_task_pool(task_pool), m_cmdu_tx(cmdu_tx),
      m_send_cmdu(std::move(send_cmdu)), m_now(std::move(now))
{
    config_fsm();
}

void CacFsm::reset()
{
    m_cac_request_radio = {};
    m_active_switch_channel_request.reset();

    m_original_channel                  = 0;
    m_original_bandwidth                = eWiFiBandwidth::BANDWIDTH_UNKNOWN;
    m_original_center_frequency         = 0;
    m_original_secondary_channel_offset = 0;

    m_wait_for_switch_channel_report = DEFAULT_WAIT_FOR_SWITCH_CHANNEL_REPORT;
}

void CacFsm::config_fsm()
{
    config()

        /////////////
        //// IDLE ///
        /////////////
        .state(fsm_state::IDLE)

        .entry([&](const void *args) -> bool {
            reset();
            return true;
        })

        .on(fsm_event::CAC_REQUEST,
            {
                fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT,
                fsm_state::ERROR,
            },
            [&](TTransition &transition, const void *args) -> bool {
                if (m_cac_request_radio.radio_uid != net::network_utils::ZERO_MAC) {
                    LOG(ERROR) << "another cac request in progress, ignoring.";
                    // TODO: send preference report with an error
                    // https://jira.prplfoundation.org/browse/PPM-1090
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }

                auto msg = *(static_cast<std::shared_ptr<wfa_map::tlvProfile2CacRequest> *>(
                    const_cast<void *>(args)));

                if (!msg) {
                    LOG(ERROR) << "Received CAC request without data";
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }

                // escape on currently non supported. PPM-1313.
                if (msg->number_of_cac_radios() != 1) {
                    LOG(ERROR) << "Only one radio is supported for cac request";
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }
                auto request_radio  = msg->cac_radios(0);
                m_cac_request_radio = std::get<1>(request_radio);

                LOG(DEBUG) << "CAC Request:"
                           << " radio_uid=" << m_cac_request_radio.radio_uid
                           << ", operating_class=" << m_cac_request_radio.operating_class
                           << ", channel=" << m_cac_request_radio.channel
                           << ", cac_method=" << m_cac_request_radio.cac_method_bit_field.cac_method
                           << ", cac_completion_action="
                           << m_cac_request_radio.cac_method_bit_field.cac_completion_action;

                if (m_cac_request_radio.cac_method_bit_field.cac_method !=
                    wfa_map::eCacMethod::CONTINUOUS_CAC) {
                    LOG(ERROR) << "Only continues-cac is supported";
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }

                // prepare for switch channel
                auto db = AgentDB::get();
                auto radio =
                    db->get_radio_by_mac(m_cac_request_radio.radio_uid, AgentDB::eMacType::RADIO);

                if (!radio) {
                    LOG(ERROR) << "Failed to find database record for interface: " << m_ifname;
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }

                m_ifname = radio->front.iface_name;

                if (!send_cac_request(radio)) {
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }

                // moving to next state: WAIT_FOR_SWITCH_CHANNEL_REPORT
                return true;
            })

        // it is ok to receive this event, we just stay in the same state
        .on(fsm_event::PERIODIC, fsm_state::IDLE)

        // nothing to terminate
        .on(fsm_event::CAC_TERMINATION_REQUEST, fsm_state::IDLE)

        // send preference report when SWITCH_CHANNEL_REPORT event is received in IDLE state
        .on(fsm_event::SWITCH_CHANNEL_REPORT, {fsm_state::IDLE},
            [&](TTransition &transition, const void *args) -> bool {
                auto switch_channel_report =
                    *(static_cast<std::shared_ptr<const sSwitchChannelReport> *>(
                        const_cast<void *>(args)));

                // Late reports can have no CSA payload (CAC completion or timeout).
                // Only an unsolicited channel change belongs to this IDLE handler.
                if (!switch_channel_report || switch_channel_report->original_request ||
                    !switch_channel_report->switch_channel_notification) {
                    return false;
                }
                m_ifname = switch_channel_report->switch_channel_notification->ifname;

                send_preference_report();
                return false;
            })

        /////////////////////////////////////////
        //// WAIT_FOR_SWITCH_CHANNEL_REPORT ///
        /////////////////////////////////////////
        .state(fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT)

        .on(fsm_event::SWITCH_CHANNEL_DURATION_TIME, {fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT},
            [&](TTransition &transition, const void *args) -> bool {
                return update_wait_time(args);
            })

        .on(fsm_event::SWITCH_CHANNEL_REPORT,
            {fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT, fsm_state::IDLE,
             fsm_state::ERROR},
            [&](TTransition &transition, const void *args) -> bool {
                auto switch_channel_report =
                    *(static_cast<std::shared_ptr<const sSwitchChannelReport> *>(
                        const_cast<void *>(args)));

                if (!switch_channel_report || !report_matches_request(*switch_channel_report)) {
                    return false;
                }
                if (!report_succeeded(*switch_channel_report)) {
                    send_preference_report();
                    LOG(ERROR) << "CAC channel switch failed for " << m_ifname;
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }

                send_preference_report();

                // depends on the completion action:
                // remain on the same channel --> IDLE
                // go back to the previous -->
                // 1. send switch channel request
                // 2. --> WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT

                // stay on channel
                if (m_cac_request_radio.cac_method_bit_field.cac_completion_action ==
                    wfa_map::tlvProfile2CacRequest::eCacCompletionAction::REMAIN_ON_CHANNEL) {
                    // we were requested to stay on the same channel
                    // therefore we are done
                    LOG(DEBUG)
                        << "Controller requested to stay on the same channel after performing CAC";
                    transition.change_destination(fsm_state::IDLE);
                    return true;
                } else if (m_cac_request_radio.cac_method_bit_field.cac_completion_action ==
                           wfa_map::tlvProfile2CacRequest::eCacCompletionAction::
                               RETURN_PREVIOUS_CHANNEL) {

                    LOG(DEBUG) << "Controller requested to return to original channel after "
                                  "performing CAC";
                    // save the time point we started the switch channel
                    m_switch_channel_start_time_point = m_now();

                    // reset the wait time
                    m_wait_for_switch_channel_report = DEFAULT_WAIT_FOR_SWITCH_CHANNEL_REPORT;

                    // sending switch channel request (II)
                    m_active_switch_channel_request =
                        send_switch_channel_request(m_original_channel, m_original_bandwidth);
                    if (!m_active_switch_channel_request) {
                        transition.change_destination(fsm_state::ERROR);
                    }

                    // switch to the first state in the list: WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT
                    return true;

                } else {
                    LOG(ERROR) << "Unknown completion action";
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }

                return true;
            })

        // SwitchChannelFsm correlates raw CAC events and publishes a terminal report.
        .on(fsm_event::CAC_STARTED_NOTIFICATION, fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT)
        .on(fsm_event::CAC_COMPLETED_NOTIFICATION, fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT)

        .on(fsm_event::PERIODIC, {fsm_state::WAIT_FOR_SWITCH_CHANNEL_REPORT, fsm_state::ERROR},
            [&](TTransition &transition, const void *args) -> bool {
                if (is_timeout_waiting_for_switch_channel_report()) {
                    LOG(ERROR) << "Timeout occurred waiting for switch channel report (I)";
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }
                // returning false means "stay in the same state"
                return false;
            })

        .on(fsm_event::CAC_TERMINATION_REQUEST, fsm_state::WAIT_FOR_CAC_TERMINATION,
            [&](TTransition &transition, const void *args) -> bool {
                auto cac_termination =
                    *(static_cast<std::shared_ptr<wfa_map::tlvProfile2CacTermination> *>(
                        const_cast<void *>(args)));
                if (!cac_termination) {
                    LOG(ERROR) << "null cac termination. ignoring";
                    return false;
                }

                // validate that the termination refers to the running cac request
                auto termination_cac_radio = cac_termination->cac_radios(0);
                if (!std::get<0>(termination_cac_radio)) {
                    LOG(ERROR) << "empty cac termination. ignoring";
                    return false;
                }

                bool termination_on_active_request =
                    std::get<1>(termination_cac_radio).radio_uid == m_cac_request_radio.radio_uid &&
                    std::get<1>(termination_cac_radio).operating_class ==
                        m_cac_request_radio.operating_class &&
                    std::get<1>(termination_cac_radio).channel == m_cac_request_radio.channel;

                if (!termination_on_active_request) {
                    LOG(WARNING) << "requested cac termination not on the active request. ignoring";
                    return false;
                }

                // 2. send stop-cac request to the driver

                // save the time point we asked for stopping the cac
                m_terminate_cac_start_time_point = m_now();

                // create cancel cac request
                auto cancel_cac_request = message_com::create_vs_message<
                    beerocks_message::cACTION_BACKHAUL_HOSTAP_CANCEL_ACTIVE_CAC_REQUEST>(m_cmdu_tx);
                if (!cancel_cac_request) {
                    LOG(ERROR) << "Failed to build cancel cac message";
                    return false;
                }

                cancel_cac_request->cs_params().channel              = m_original_channel;
                cancel_cac_request->cs_params().bandwidth            = m_original_bandwidth;
                cancel_cac_request->cs_params().vht_center_frequency = m_original_center_frequency;
                cancel_cac_request->cs_params().channel_ext_above_primary =
                    m_original_secondary_channel_offset;

                bool cmdu_sent = m_send_cmdu(m_ifname, m_cmdu_tx);
                if (!cmdu_sent) {
                    LOG(ERROR) << "Failed to send cancel cac request";
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }

                // moving to next state: WAIT_FOR_CAC_TERMINATION
                return true;
            })

        //////////////////////////////////////////
        //// WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT ///
        //////////////////////////////////////////
        .state(fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT)

        .on(fsm_event::SWITCH_CHANNEL_DURATION_TIME,
            {fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT},
            [&](TTransition &transition, const void *args) -> bool {
                return update_wait_time(args);
            })

        .on(fsm_event::SWITCH_CHANNEL_REPORT, {fsm_state::IDLE, fsm_state::ERROR},
            [&](TTransition &transition, const void *args) -> bool {
                auto report =
                    *static_cast<const std::shared_ptr<const sSwitchChannelReport> *>(args);
                if (!report || !report_matches_request(*report)) {
                    return false;
                }
                if (!report_succeeded(*report)) {
                    LOG(ERROR) << "Failed to restore original channel on " << m_ifname;
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }
                LOG(DEBUG) << "Restored original channel " << +m_original_channel << " on "
                           << m_ifname;
                return true;
            })
        .on(fsm_event::PERIODIC,
            {fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT, fsm_state::ERROR},
            [&](TTransition &transition, const void *args) -> bool {
                // check timeout
                if (is_timeout_waiting_for_switch_channel_report()) {
                    LOG(ERROR) << "Timeout occurred waiting for switch channel report (II)";
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }
                // returning false means "stay in the same state"
                return false;
            })

        .on(fsm_event::CAC_TERMINATION_REQUEST,
            fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT)

        /////////////////////////////////
        //// WAIT_FOR_CAC_TERMINATION ///
        /////////////////////////////////
        .state(fsm_state::WAIT_FOR_CAC_TERMINATION)

        .on(fsm_event::CAC_TERMINATION_RESPONSE, {fsm_state::IDLE, fsm_state::ERROR},
            [&](TTransition &transition, const void *args) -> bool {
                auto success = (static_cast<uint8_t *>(const_cast<void *>(args)));
                if (!success) {
                    LOG(ERROR) << "null cac-termination success pointer";
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }

                if (!*success) {
                    LOG(ERROR) << "CAC termination failed for " << m_ifname;
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }
                m_task_pool.send_event(eTaskEvent::SWITCH_CHANNEL_REQUEST_CANCELLED,
                                       m_active_switch_channel_request);
                // just reporting
                LOG(DEBUG) << "cancel cac reported as ended with: " << bool(*success);
                return true;
            })

        .on(fsm_event::PERIODIC,
            {fsm_state::WAIT_FOR_SWITCH_BACK_TO_ORIGINAL_CHANNEL_REPORT, fsm_state::ERROR},
            [&](TTransition &transition, const void *args) -> bool {
                // check timeout
                if (is_timeout_waiting_for_cac_termination()) {
                    LOG(ERROR) << "timeout occurred waiting for cac termination";
                    transition.change_destination(fsm_state::ERROR);
                    return true;
                }
                // returning false means "stay in the same state"
                return false;
            })

        //////////////
        //// ERROR ///
        //////////////
        .state(fsm_state::ERROR)

        // for now - just going back to idle
        .on(fsm_event::PERIODIC, fsm_state::IDLE)
        .on(fsm_event::CAC_TERMINATION_REQUEST, fsm_state::ERROR);

    start();
}

bool CacFsm::send_preference_report()
{
    auto db    = AgentDB::get();
    auto radio = db->radio(m_ifname);
    if (!radio) {
        LOG(ERROR) << "Cannot request channel preference report for interface " << m_ifname;
        return false;
    }

    // A channels-list response alone does not trigger a preference report, notably
    // when CAC was already cleared and no CAC notifications occurred.
    LOG(DEBUG) << "Requesting channel preference report for " << m_ifname;
    m_task_pool.send_event(eTaskEvent::CHANNEL_PREFERENCE_REPORT_REQUEST,
                           std::make_shared<sMacAddr>(radio->front.iface_mac));
    return true;
}

std::shared_ptr<sSwitchChannelRequest>
CacFsm::send_switch_channel_request(uint8_t channel, beerocks::eWiFiBandwidth bandwidth,
                                    bool wait_for_cac)
{
    // sending switch channel request
    auto switch_channel_request = std::make_shared<sSwitchChannelRequest>();
    if (!switch_channel_request) {
        LOG(ERROR) << "unable to create switch channel request data"
                   << " for "
                   << "channel: " << channel << " bandwidth: " << bandwidth;
        return nullptr;
    }

    switch_channel_request->ifname       = m_ifname;
    switch_channel_request->channel      = channel;
    switch_channel_request->freq_type    = eFreqType::FREQ_5G;
    switch_channel_request->bandwidth    = bandwidth;
    switch_channel_request->wait_for_cac = wait_for_cac;

    LOG(DEBUG) << *switch_channel_request;

    m_task_pool.send_event(eTaskEvent::SWITCH_CHANNEL_REQUEST, switch_channel_request);

    return switch_channel_request;
}

bool CacFsm::send_cac_request(beerocks::AgentDB::sRadio *db_radio)
{
    if (!db_radio) {
        return false;
    }
    auto channel_info = db_radio->channels_list.find(m_cac_request_radio.channel);
    if (channel_info == db_radio->channels_list.end()) {
        LOG(ERROR) << "the channel " << m_cac_request_radio.channel << " is not supported for "
                   << m_ifname;
        return false;
    }

    // find the bandwidth based on the operating class in the request
    beerocks::eWiFiBandwidth bandwidth =
        son::wireless_utils::operating_class_to_bandwidth(m_cac_request_radio.operating_class);

    // A CSA on a DFS channel which still needs CAC is only a tuning notification.
    // Check every constituent channel so a cleared primary does not hide an uncleared secondary.
    WifiChannel target(m_cac_request_radio.channel, eFreqType::FREQ_5G, bandwidth);
    auto channels =
        son::wireless_utils::get_5g_20MHz_channels(bandwidth, target.get_center_frequency());
    if (channels.empty()) {
        LOG(ERROR) << "Invalid CAC channel/bandwidth for " << m_ifname;
        return false;
    }
    bool wait_for_cac = false;
    for (auto channel : channels) {
        if (!son::wireless_utils::is_dfs_channel(channel, eFreqType::FREQ_5G)) {
            continue;
        }
        auto info = db_radio->channels_list.find(channel);
        if (info == db_radio->channels_list.end() ||
            info->second.dfs_state != beerocks_message::eDfsState::AVAILABLE) {
            wait_for_cac = true;
        }
    }

    // save current channel - to know where to switch back if needed
    m_original_channel          = db_radio->wifi_channel.get_channel();
    m_original_bandwidth        = db_radio->wifi_channel.get_bandwidth();
    m_original_center_frequency = db_radio->wifi_channel.get_center_frequency();
    if (m_original_bandwidth == eWiFiBandwidth::BANDWIDTH_20) {
        m_original_secondary_channel_offset = 0;
    } else {
        m_original_secondary_channel_offset =
            db_radio->wifi_channel.get_ext_above_primary() ? 1 : -1;
    }

    LOG(DEBUG) << "Setting m_original_channel = " << m_original_channel;

    // save the time point we started the switch channel
    m_switch_channel_start_time_point = m_now();

    // sending switch channel request (I)
    m_active_switch_channel_request =
        send_switch_channel_request(m_cac_request_radio.channel, bandwidth, wait_for_cac);
    if (!m_active_switch_channel_request) {
        LOG(ERROR) << "Failed to send switch channel request.";
        return false;
    }

    // store the request in the database
    db_radio->last_switch_channel_request = m_active_switch_channel_request;

    return true;
}

bool CacFsm::update_wait_time(const void *args)
{
    auto wait = *static_cast<const std::shared_ptr<const sSwitchChannelDurationTime> *>(args);
    if (!wait || wait->ifname != m_ifname ||
        wait->original_request != m_active_switch_channel_request ||
        wait->duration_sec <= std::chrono::seconds::zero() ||
        wait->started_at < m_switch_channel_start_time_point || wait->started_at > m_now()) {
        return false;
    }
    // Each wait starts at its triggering notification (CAC start or completion),
    // rather than at the earlier channel-switch request.
    // Let SwitchChannelFsm publish its timeout before this task expires independently.
    m_switch_channel_start_time_point = wait->started_at;
    m_wait_for_switch_channel_report  = wait->duration_sec + std::chrono::seconds(5);
    return false;
}

bool CacFsm::report_matches_request(const sSwitchChannelReport &report) const
{
    // Task events retain the same shared request. Pointer identity rejects old operations,
    // including another request for exactly the same radio and channel.
    return m_active_switch_channel_request &&
           report.original_request == m_active_switch_channel_request;
}

bool CacFsm::report_succeeded(const sSwitchChannelReport &report) const
{
    if (report.compute_switch_channel_report_status() != eSwitchChannelReportStatus::SUCCESS) {
        return false;
    }
    const auto &request  = *m_active_switch_channel_request;
    const auto &switched = report.switch_channel_notification;
    if (!switched || !switched->switched ||
        !request.matches_channel_switch(*switched, request.wait_for_cac)) {
        return false;
    }
    if (request.wait_for_cac || report.cac_happened) {
        const auto &completed = report.cac_completed_notification;
        return report.cac_happened && completed && request.matches_cac_completed(*completed) &&
               completed->cac_completed_params.success == 1;
    }
    return true;
}

bool CacFsm::is_timeout_waiting_for_switch_channel_report()
{
    // check switch channel timeout
    return (m_now() - m_switch_channel_start_time_point) > m_wait_for_switch_channel_report;
}

bool CacFsm::is_timeout_waiting_for_cac_termination()
{
    // check cac termination timeout
    return (m_now() - m_terminate_cac_start_time_point) > m_max_wait_for_cac_termination;
}

} // namespace coordinated_cac
} // namespace beerocks
