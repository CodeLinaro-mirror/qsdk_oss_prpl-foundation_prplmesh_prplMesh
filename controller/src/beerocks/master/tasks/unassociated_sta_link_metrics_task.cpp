#include "unassociated_sta_link_metrics_task.h"

#include "../son_actions.h"
#include <tlvf/wfa_map/tlvUnassociatedStaLinkMetricsQuery.h>
#include <tlvf/wfa_map/tlvUnassociatedStaLinkMetricsResponse.h>

namespace son {

UnassociatedStaLinkMetricsTask::UnassociatedStaLinkMetricsTask(db &database_,
                                                               ieee1905_1::CmduMessageTx &cmdu_tx_)
    : database(database_), cmdu_tx(cmdu_tx_)
{
    LOG(DEBUG) << "Start UnassociatedStaLinkMetricsTask(id=" << id << ")";
    database.assign_unassociated_sta_link_metrics_task_id(id);
    last_query_request = std::chrono::steady_clock::now();
}

void UnassociatedStaLinkMetricsTask::work()
{
    if (database.config.link_metrics_request_interval_seconds == std::chrono::seconds::zero()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto last_seen_delta =
        std::chrono::duration_cast<std::chrono::seconds>(now - last_query_request);
    if (last_seen_delta < database.config.link_metrics_request_interval_seconds) {
        return;
    }

    std::unordered_set<sMacAddr> agents_with_unassociated_stations;
    for (const auto &station : database.get_unassociated_stations()) {
        for (const auto &agent : station.second->get_agents()) {
            agents_with_unassociated_stations.insert(agent.first);
        }
    }

    if (!agents_with_unassociated_stations.empty()) {
        if (!cmdu_tx.create(
                0, ieee1905_1::eMessageType::UNASSOCIATED_STA_LINK_METRICS_QUERY_MESSAGE)) {
            LOG(ERROR) << "Failed building message UNASSOCIATED_STA_LINK_METRICS_QUERY_MESSAGE!";
            return;
        }
        cmdu_tx.addClass<wfa_map::tlvUnassociatedStaLinkMetricsQuery>();
        for (const auto &agent_mac : agents_with_unassociated_stations) {
            son_actions::send_cmdu_to_agent(agent_mac, cmdu_tx, database);
        }
    }

    last_query_request = std::chrono::steady_clock::now();
}

bool UnassociatedStaLinkMetricsTask::handle_ieee1905_1_msg(const sMacAddr &src_mac,
                                                           ieee1905_1::CmduMessageRx &cmdu_rx)
{
    switch (cmdu_rx.getMessageType()) {
    case ieee1905_1::eMessageType::UNASSOCIATED_STA_LINK_METRICS_RESPONSE_MESSAGE: {
        return handle_cmdu_1905_unassociated_station_link_metric_response(src_mac, cmdu_rx);
    }
    default: {
        return false;
    }
    }
}

bool UnassociatedStaLinkMetricsTask::handle_cmdu_1905_unassociated_station_link_metric_response(
    const sMacAddr &src_mac, ieee1905_1::CmduMessageRx &cmdu_rx)
{
    const auto message_id = cmdu_rx.getMessageId();
    LOG(DEBUG) << "Received an Unassociated STA Link Metrics Response, mid=" << message_id
               << " from agent with src_mac=" << src_mac;

    auto unassoc_sta_link_metrics_tlv =
        cmdu_rx.getClass<wfa_map::tlvUnassociatedStaLinkMetricsResponse>();
    if (!unassoc_sta_link_metrics_tlv) {
        LOG(ERROR) << "Unassociated STA Link Metrics Response message did not contain an "
                      "Unassociated STA Link Metrics TLV!";
        return false;
    }

    // build ACK message CMDU
    const auto mid      = cmdu_rx.getMessageId();
    auto cmdu_tx_header = cmdu_tx.create(mid, ieee1905_1::eMessageType::ACK_MESSAGE);
    if (!cmdu_tx_header) {
        LOG(ERROR) << "cmdu creation of type ACK_MESSAGE, has failed";
        return false;
    }
    // send the ack
    son_actions::send_cmdu_to_agent(src_mac, cmdu_tx, database);

    auto number_of_station_entries = unassoc_sta_link_metrics_tlv->sta_list_length();
    if (number_of_station_entries == 0) {
        LOG(DEBUG) << "Unassociated STA Link Metrics Response from Agent " << src_mac
                   << " reports zero stations, nothing to do!";
        return true;
    }

    //getting reference for unassoc link metrics data storage from db
    auto &unassoc_sta_metric = database.get_unassoc_sta_map();

    for (int i = 0; i < number_of_station_entries; ++i) {
        const auto station_tuple = unassoc_sta_link_metrics_tlv->sta_list(i);
        const auto sta_metrics   = std::get<1>(station_tuple);

        if (sta_metrics.sta_mac == beerocks::net::network_utils::ZERO_MAC) {
            // skip dud entries
            LOG(DEBUG) << " Zero_MAC!!--> skipped " << tlvf::mac_to_string(sta_metrics.sta_mac);
            continue;
        }

        uint8_t operating_class_received =
            unassoc_sta_link_metrics_tlv->operating_class_of_channel_list();
        std::string mac_addr_str = tlvf::mac_to_string(sta_metrics.sta_mac);
        LOG(DEBUG) << "Controller received Unassoc STA Link Metrics Response  with operating_class="
                   << operating_class_received << " entry: " << i << " from Agent "
                   << tlvf::mac_to_string(src_mac)
                   << ", MAC: " << tlvf::mac_to_string(sta_metrics.sta_mac)
                   << ", RCPI: " << sta_metrics.uplink_rcpi_dbm_enc
                   << ", ChannelNum: " << sta_metrics.channel_number
                   << ", time_stamp(uint_32): " << sta_metrics.measurement_to_report_delta_msec;

        // Updating flag for measurement done
        database.m_measurement_done = true;
        database.m_opclass          = operating_class_received;
        son::db::sUnAssocStaInfo sta_details;
        std::string mac               = tlvf::mac_to_string(sta_metrics.sta_mac);
        sta_details.channel           = sta_metrics.channel_number;
        sta_details.rcpi              = sta_metrics.uplink_rcpi_dbm_enc;
        sta_details.measurement_delta = sta_metrics.measurement_to_report_delta_msec;
        unassoc_sta_metric[mac]       = sta_details;

        // updating the DM and the database
        auto agents = database.get_all_connected_agents();

        auto agent =
            std::find_if(agents.begin(), agents.end(), [&src_mac](std::shared_ptr<Agent> input) {
                return (tlvf::mac_to_string(input->al_mac) == tlvf::mac_to_string(src_mac));
            });
        if (agent == end(agents)) {
            LOG(ERROR) << "Failed to get agent with mac_addr: " << tlvf::mac_to_string(src_mac);
            return false;
        }
        sMacAddr radio_mac_address(beerocks::net::network_utils::ZERO_MAC);
        for (auto &radio : (*agent)->radios) {
            for (auto &operating_class : radio.second->scan_capabilities.operating_classes) {
                if (operating_class_received == operating_class.first) {
                    radio_mac_address = radio.first;
                }
            }
        }

        if (tlvf::mac_to_string(radio_mac_address) ==
            beerocks::net::network_utils::ZERO_MAC_STRING) {
            LOG(ERROR) << "Agent with mac_addr: " << tlvf::mac_to_string((*agent)->al_mac)
                       << " has no radio that supports operating_class " << operating_class_received
                       << " !!, un_station stats will not get updated! ";
            return false;
        }

        auto radio = database.get_radio(src_mac, radio_mac_address);
        if (!radio) {
            LOG(ERROR) << "Failed to get radio with in agent with mac_addr: " << src_mac
                       << " and radio_uid: " << tlvf::mac_to_string(radio_mac_address);
            return false;
        }
        if (radio->dm_path.empty()) {
            LOG(ERROR) << "radio dm path is empty! , unassociated station DM will not be updated! ";
        }

        auto un_stat_database = database.get_unassociated_stations().get(sta_metrics.sta_mac);
        if (un_stat_database != nullptr) {
            auto agent_radio = un_stat_database->get_agents().find(src_mac);
            if (agent_radio != un_stat_database->get_agents().end()) {
                //update the station with the mac_addr of the radio that is monitoring it
                un_stat_database->set_radio_mac(src_mac, radio_mac_address);
            } else {
                LOG(WARNING) << "is agent: " << tlvf::mac_to_string(src_mac)
                             << " non intentially monitoring station with mac_address: "
                             << tlvf::mac_to_string(un_stat_database->get_mac_Address()) << " ? ";
            }

            UnassociatedStation::Stats stats;
            stats.uplink_rcpi_dbm_enc = sta_metrics.uplink_rcpi_dbm_enc;

            time_t received_time = sta_metrics.measurement_to_report_delta_msec;
            char buf[sizeof "2011-10-08T07:07:09Z"];
            strftime(buf, sizeof buf, "%FT%TZ", gmtime(&received_time));
            stats.time_stamp = buf;

            database.update_unassociated_station_stats(sta_metrics.sta_mac, stats, radio->dm_path);
        }
    }
    return true;
}

void UnassociatedStaLinkMetricsTask::handle_event(int event_enum_value, void *event_obj)
{
    switch (eEvent(event_enum_value)) {
    case UNASSOC_STA_LINK_METRICS_QUERY: {
        //Take request
        auto unassoc_sta_link_metric_event =
            reinterpret_cast<const sUnAssociatedLinkMetricsQueryEvent *>(event_obj);

        if (!cmdu_tx.create(
                0, ieee1905_1::eMessageType::UNASSOCIATED_STA_LINK_METRICS_QUERY_MESSAGE)) {
            LOG(ERROR) << "Failed building UNASSOCIATED_STA_LINK_METRICS_QUERY_MESSAGE message!";
            return;
        }

        auto tlvUnassociatedStaLinkMetricsQuery =
            cmdu_tx.addClass<wfa_map::tlvUnassociatedStaLinkMetricsQuery>();
        if (!tlvUnassociatedStaLinkMetricsQuery) {
            LOG(ERROR) << "addClass tlvUnassociatedStaLinkMetricsQuery failed!";
            return;
        }
        tlvUnassociatedStaLinkMetricsQuery->operating_class_of_channel_list() =
            unassoc_sta_link_metric_event->opClass;
        auto unassocChanList = tlvUnassociatedStaLinkMetricsQuery->create_channel_list();
        unassocChanList->channel_number() = unassoc_sta_link_metric_event->channel;

        if (!unassocChanList->alloc_sta_list(1)) {
            LOG(ERROR) << "allocate_sta_list failed";
            return;
        }

        auto &unassoc_sta_list = std::get<1>(unassocChanList->sta_list(0));
        unassoc_sta_list       = unassoc_sta_link_metric_event->unassoc_sta_mac;

        // Add channel_list object to TLV
        if (!tlvUnassociatedStaLinkMetricsQuery->add_channel_list(unassocChanList)) {
            LOG(ERROR) << "add_channel_list() failed";
            return;
        }

        // TODO: Check the capability of agent and send only to those which support unassoc sta link metric
        for (const auto &agent : database.get_all_connected_agents()) {
            son_actions::send_cmdu_to_agent(agent->al_mac, cmdu_tx, database);
        }
        break;
    }
    }
}

} // namespace son
