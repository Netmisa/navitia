/* Copyright © 2001-2014, Canal TP and/or its affiliates. All rights reserved.
  
This file is part of Navitia,
    the software to build cool stuff with public transport.
 
Hope you'll enjoy and contribute to this project,
    powered by Canal TP (www.canaltp.fr).
Help us simplify mobility and open public transport:
    a non ending quest to the responsive locomotion way of traveling!
  
LICENCE: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
   
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Affero General Public License for more details.
   
You should have received a copy of the GNU Affero General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
  
Stay tuned using
twitter @navitia 
IRC #navitia on freenode
https://groups.google.com/d/forum/navitia
www.navitia.io
*/

#include "traffic_reports_api.h"
#include "type/pb_converter.h"
#include "ptreferential/ptreferential.h"
#include "utils/logger.h"

namespace bt = boost::posix_time;

namespace navitia { namespace disruption {

namespace { // anonymous namespace

using DisruptionSet = std::set<boost::shared_ptr<type::disruption::Impact>, Less>;

struct NetworkDisrupt {
    type::idx_t idx;
    const type::Network* network = nullptr;
    DisruptionSet network_disruptions;
    //we use a vector of pair because we need to sort by the priority of the impacts
    std::vector<std::pair<const type::Line*, DisruptionSet>> lines;
    std::vector<std::pair<const type::StopArea*, DisruptionSet>> stop_areas;
};

class TrafficReport {
private:
    std::vector<NetworkDisrupt> disrupts;
    log4cplus::Logger logger;

    NetworkDisrupt& find_or_create(const type::Network* network);
    void add_stop_areas(const std::vector<type::idx_t>& network_idx,
                      const std::string& filter,
                      const std::vector<std::string>& forbidden_uris,
                      const type::Data &d,
                      const boost::posix_time::ptime now);

    void add_networks(const std::vector<type::idx_t>& network_idx,
                      const type::Data &d,
                      const boost::posix_time::ptime now);
    void add_lines(const std::string& filter,
                      const std::vector<std::string>& forbidden_uris,
                      const type::Data &d,
                      const boost::posix_time::ptime now);
    void sort_disruptions();
public:
    TrafficReport(): logger(log4cplus::Logger::getInstance(LOG4CPLUS_TEXT("logger"))) {}

    void disruptions_list(const std::string& filter,
                    const std::vector<std::string>& forbidden_uris,
                    const type::Data &d,
                    const boost::posix_time::ptime now);

    const std::vector<NetworkDisrupt>& get_disrupts() const {
        return this->disrupts;
    }

    size_t get_disrupts_size() {
        return this->disrupts.size();
    }
};

static int min_priority(const DisruptionSet& disruptions){
    int min = std::numeric_limits<int>::max();
    for(const auto& impact: disruptions){
        if(!impact->severity) continue;
        if(impact->severity->priority < min){
            min = impact->severity->priority;
        }
    }
    return min;
}


NetworkDisrupt& TrafficReport::find_or_create(const type::Network* network){
    auto find_predicate = [&](const NetworkDisrupt& network_disrupt) {
        return network == network_disrupt.network;
    };
    auto it = boost::find_if(this->disrupts, find_predicate);
    if(it == this->disrupts.end()){
        NetworkDisrupt dist;
        dist.network = network;
        dist.idx = this->disrupts.size();
        this->disrupts.push_back(std::move(dist));
        return disrupts.back();
    }
    return *it;
}

void TrafficReport::add_stop_areas(const std::vector<type::idx_t>& network_idx,
                      const std::string& filter,
                      const std::vector<std::string>& forbidden_uris,
                      const type::Data& d,
                      const boost::posix_time::ptime now){

    for(auto idx : network_idx){
        const auto* network = d.pt_data->networks[idx];
        std::string new_filter = "network.uri=" + network->uri;
        if(!filter.empty()){
            new_filter += " and " + filter;
        }
        std::vector<type::idx_t> line_list;

       try{
            line_list = ptref::make_query(type::Type_e::StopArea, new_filter,
                    forbidden_uris, d);
        } catch(const ptref::parsing_error &parse_error) {
            LOG4CPLUS_WARN(logger, "Disruption::add_stop_areas : Unable to parse filter "
                                + parse_error.more);
        } catch(const ptref::ptref_error &ptref_error){
            LOG4CPLUS_WARN(logger, "Disruption::add_stop_areas : ptref : "  + ptref_error.more);
        }
        for(auto stop_area_idx: line_list){
            const auto* stop_area = d.pt_data->stop_areas[stop_area_idx];
            auto v = stop_area->get_publishable_messages(now);
            for(const auto* stop_point: stop_area->stop_point_list){
                auto vsp = stop_point->get_publishable_messages(now);
                v.insert(v.end(), vsp.begin(), vsp.end());
            }
            if (!v.empty()){
                NetworkDisrupt& dist = this->find_or_create(network);
                auto find_predicate = [&](const std::pair<const type::StopArea*, DisruptionSet>& item) {
                    return item.first == stop_area;
                };
                auto it = boost::find_if(dist.stop_areas, find_predicate);
                if(it == dist.stop_areas.end()){
                    dist.stop_areas.push_back(std::make_pair(stop_area, DisruptionSet(v.begin(), v.end())));
                }else{
                    it->second.insert(v.begin(), v.end());
                }
            }
        }
    }
}

void TrafficReport::add_networks(const std::vector<type::idx_t>& network_idx,
                      const type::Data &d,
                      const boost::posix_time::ptime now){

    for(auto idx : network_idx){
        const auto* network = d.pt_data->networks[idx];
        if (network->has_publishable_message(now)){
            auto& res = this->find_or_create(network);
            auto v = network->get_publishable_messages(now);
            res.network_disruptions.insert(v.begin(), v.end());
        }
    }
}

void TrafficReport::add_lines(const std::string& filter,
                      const std::vector<std::string>& forbidden_uris,
                      const type::Data& d,
                      const boost::posix_time::ptime now){

    std::vector<type::idx_t> line_list;
    try {
        line_list  = ptref::make_query(type::Type_e::Line, filter, forbidden_uris, d);
    } catch(const ptref::parsing_error &parse_error) {
        LOG4CPLUS_WARN(logger, "Disruption::add_lines : Unable to parse filter " + parse_error.more);
    } catch(const ptref::ptref_error &ptref_error){
        LOG4CPLUS_WARN(logger, "Disruption::add_lines : ptref : "  + ptref_error.more);
    }
    for(auto idx : line_list){
        const auto* line = d.pt_data->lines[idx];
        auto v = line->get_publishable_messages(now);
        for(const auto* route: line->route_list){
            auto vr = route->get_publishable_messages(now);
            v.insert(v.end(), vr.begin(), vr.end());
        }
        if (!v.empty()){
            NetworkDisrupt& dist = this->find_or_create(line->network);
            auto find_predicate = [&](const std::pair<const type::Line*, DisruptionSet>& item) {
                return line == item.first;
            };
            auto it = boost::find_if(dist.lines, find_predicate);
            if(it == dist.lines.end()){
                dist.lines.push_back(std::make_pair(line, DisruptionSet(v.begin(), v.end())));
            }else{
                it->second.insert(v.begin(), v.end());
            }
        }
    }
}

void TrafficReport::sort_disruptions(){
    auto sort_disruption = [&](const NetworkDisrupt& d1, const NetworkDisrupt& d2){
            return d1.network->idx < d2.network->idx;
    };

    auto sort_lines = [&](const std::pair<const type::Line*, DisruptionSet>& l1,
                          const std::pair<const type::Line*, DisruptionSet>& l2) {
        int p1 = min_priority(l1.second);
        int p2 = min_priority(l2.second);
        if(p1 != p2){
            return p1 < p2;
        }else if(l1.first->code != l2.first->code){
            return l1.first->code < l2.first->code;
        }else{
            return l1.first->name < l2.first->name;
        }
    };

    std::sort(this->disrupts.begin(), this->disrupts.end(), sort_disruption);
    for(auto& disrupt : this->disrupts){
        std::sort(disrupt.lines.begin(), disrupt.lines.end(), sort_lines);
    }

}

void TrafficReport::disruptions_list(const std::string& filter,
                        const std::vector<std::string>& forbidden_uris,
                        const type::Data& d,
                        const boost::posix_time::ptime now){

    std::vector<type::idx_t> network_idx = ptref::make_query(type::Type_e::Network, filter,
                                                             forbidden_uris, d);
    add_networks(network_idx, d, now);
    add_lines(filter, forbidden_uris, d, now);
    add_stop_areas(network_idx, filter, forbidden_uris, d, now);
    sort_disruptions();
}

} // anonymous namespace

pbnavitia::Response traffic_reports(const navitia::type::Data& d,
                                uint64_t posix_now_dt,
                                const size_t depth,
                                size_t count,
                                size_t start_page,
                                const std::string& filter,
                                const std::vector<std::string>& forbidden_uris) {
    pbnavitia::Response pb_response;

    bt::ptime now_dt = bt::from_time_t(posix_now_dt);
    auto action_period = bt::time_period(now_dt, bt::seconds(1));

    TrafficReport result;
    try {
        result.disruptions_list(filter, forbidden_uris, d, now_dt);
    } catch(const ptref::parsing_error& parse_error) {
        fill_pb_error(pbnavitia::Error::unable_to_parse,
                "Unable to parse filter" + parse_error.more, pb_response.mutable_error());
        return pb_response;
    } catch(const ptref::ptref_error& ptref_error) {
        fill_pb_error(pbnavitia::Error::bad_filter,
                "ptref : "  + ptref_error.more, pb_response.mutable_error());
        return pb_response;
    }

    size_t total_result = result.get_disrupts_size();
    std::vector<NetworkDisrupt> disrupts = paginate(result.get_disrupts(), count, start_page);
    for (const NetworkDisrupt& dist: disrupts) {
        pbnavitia::Disruptions* pb_disruption = pb_response.add_disruptions();
        pbnavitia::Network* pb_network = pb_disruption->mutable_network();
        for(const auto& impact: dist.network_disruptions){
            fill_message(*impact, d, pb_network, depth-1, now_dt, action_period);
        }
        navitia::fill_pb_object(dist.network, d, pb_network, depth, bt::not_a_date_time, action_period, false);
        for (const auto& line_item: dist.lines) {
            pbnavitia::Line* pb_line = pb_disruption->add_lines();
            navitia::fill_pb_object(line_item.first, d, pb_line, depth-1, bt::not_a_date_time, action_period, false);
            for(const auto& impact: line_item.second){
                fill_message(*impact, d, pb_line, depth-1, now_dt, action_period);
            }
        }
        for (const auto& sa_item: dist.stop_areas) {
            pbnavitia::StopArea* pb_stop_area = pb_disruption->add_stop_areas();
            navitia::fill_pb_object(sa_item.first, d, pb_stop_area, depth-1,
                                    bt::not_a_date_time, action_period, false);
            for(const auto& impact: sa_item.second){
                fill_message(*impact, d, pb_stop_area, depth-1, now_dt, action_period);
            }
        }
    }
    auto pagination = pb_response.mutable_pagination();
    pagination->set_totalresult(total_result);
    pagination->set_startpage(start_page);
    pagination->set_itemsperpage(count);
    pagination->set_itemsonpage(pb_response.disruptions_size());

    if (pb_response.disruptions_size() == 0) {
        fill_pb_error(pbnavitia::Error::no_solution, "no solution found for this disruption",
        pb_response.mutable_error());
        pb_response.set_response_type(pbnavitia::NO_SOLUTION);
    }
    return pb_response;
}

}}//namespace navitia::disruption
