#pragma once

#include "time_tables/departure_boards.h"
#include "ed/build_helper.h"
#include "tests/utils_test.h"

/**
 * Data set for departure board
 */
struct calendar_fixture {
    ed::builder b;
    boost::gregorian::date beg;
    boost::gregorian::date end_of_year;

    navitia::type::VehicleJourney* vj_week;
    navitia::type::VehicleJourney* vj_week_bis;
    navitia::type::VehicleJourney* vj_weekend;
    navitia::type::VehicleJourney* vj_all;
    navitia::type::VehicleJourney* vj_wednesday;
    calendar_fixture() : b("20120614", "departure board") {
        //2 vj during the week
        b.vj("line:A", "1", "", true, "week")("stop1", 10 * 3600, 10 * 3600 + 10 * 60)("stop2", 12 * 3600, 12 * 3600 + 10 * 60);
        b.vj("line:A", "101", "", true, "week_bis")("stop1", 11 * 3600, 11 * 3600 + 10 * 60)("stop2", 14 * 3600, 14 * 3600 + 10 * 60);
        //NOTE: we give a first random validity pattern because the builder try to factorize them

        //only one on the week end
        b.vj("line:A", "10101", "", true, "weekend")("stop1", 20 * 3600, 20 * 3600 + 10 * 60)("stop2", 21 * 3600, 21 * 3600 + 10 * 60);

        // and one everytime
        auto& builder_vj = b.vj("line:A", "1100101", "", true, "all")("stop1", "15:00"_t, "15:10"_t)
                                                                     ("stop2", "16:00"_t, "16:10"_t);
        // Add a comment on the vj
        auto& pt_data =  *(b.data->pt_data);
        pt_data.comments.add(builder_vj.vj, "vj comment");

        // and wednesday that will not be matched to any cal
        b.vj("line:A", "110010011", "", true, "wednesday")("stop1", 17 * 3600, 17 * 3600 + 10 * 60)("stop2", 18 * 3600, 18 * 3600 + 10 * 60);

        // Check partial terminus tag
        b.vj("A", "10111111", "", true, "vj1", "")("Tstop1", 10*3600, 10*3600)
                                                  ("Tstop2", 10*3600 + 30*60, 10*3600 + 30*60);
        b.vj("A", "10111111", "", true, "vj2", "")("Tstop1", 10*3600 + 30*60, 10*3600 + 30*60)
                                                  ("Tstop2", 11*3600,11*3600)
                                                  ("Tstop3", 11*3600 + 30*60,36300 + 30*60);
        // Check date_time_estimated at stoptime
        b.vj("B", "10111111", "", true, "date_time_estimated", "")
            ("ODTstop1", 10*3600, 10*3600)
            ("ODTstop2", 10*3600 + 30*60, 10*3600 + 30*60);
        // Check on_demand_transport at stoptime
        b.vj("B", "10111111", "", true, "on_demand_transport", "")
            ("ODTstop1", 11*3600, 11*3600)
            ("ODTstop2", 11*3600 + 30*60, 11*3600 + 30*60);

        b.finish();
        b.data->build_uri();
        beg = b.data->meta->production_date.begin();
        end_of_year = beg + boost::gregorian::years(1) + boost::gregorian::days(1);

        navitia::type::VehicleJourney* vj = pt_data.vehicle_journeys_map["on_demand_transport"];
        vj->stop_time_list[0].set_odt(true);

        vj = pt_data.vehicle_journeys_map["date_time_estimated"];
        vj->stop_time_list[0].set_date_time_estimated(true);

        vj_week = pt_data.vehicle_journeys_map["week"];
        vj_week->base_validity_pattern()->add(beg, end_of_year, std::bitset<7>{"1111100"});
        vj_week_bis = pt_data.vehicle_journeys_map["week_bis"];
        vj_week_bis->base_validity_pattern()->add(beg, end_of_year, std::bitset<7>{"1111100"});
        vj_weekend = pt_data.vehicle_journeys_map["weekend"];
        vj_weekend->base_validity_pattern()->add(beg, end_of_year, std::bitset<7>{"0000011"});
        vj_all = pt_data.vehicle_journeys_map["all"];
        vj_all->base_validity_pattern()->add(beg, end_of_year, std::bitset<7>{"1111111"});
        vj_wednesday = pt_data.vehicle_journeys_map["wednesday"];
        vj_wednesday->base_validity_pattern()->add(beg, end_of_year, std::bitset<7>{"0010000"});

        //we now add 2 similar calendars
        auto week_cal = new navitia::type::Calendar(b.data->meta->production_date.begin());
        week_cal->uri = "week_cal";
        week_cal->active_periods.push_back({beg, end_of_year});
        week_cal->week_pattern = std::bitset<7>{"1111100"};
        pt_data.calendars.push_back(week_cal);

        auto weekend_cal = new navitia::type::Calendar(b.data->meta->production_date.begin());
        weekend_cal->uri = "weekend_cal";
        weekend_cal->active_periods.push_back({beg, end_of_year});
        weekend_cal->week_pattern = std::bitset<7>{"0000011"};
        pt_data.calendars.push_back(weekend_cal);

        auto not_associated_cal = new navitia::type::Calendar(b.data->meta->production_date.begin());
        not_associated_cal->uri = "not_associated_cal";
        not_associated_cal->active_periods.push_back({beg, end_of_year});
        not_associated_cal->week_pattern = std::bitset<7>{"0010000"};
        pt_data.calendars.push_back(not_associated_cal); //not associated to the line

        //both calendars are associated to the line
        b.lines["line:A"]->calendar_list.push_back(week_cal);
        b.lines["line:A"]->calendar_list.push_back(weekend_cal);
        for(auto r: pt_data.routes){
            r->destination = b.sas.find("stop2")->second;
        }

        auto it_sa = b.sas.find("Tstop3");
        auto it_rt = pt_data.routes_map.find("A:1");
        it_rt->second->destination = it_sa->second;

        // load metavj calendar association from database (association is tested in ed/tests/associated_calendar_test.cpp)
        navitia::type::AssociatedCalendar* associated_calendar_for_week = new navitia::type::AssociatedCalendar();
        navitia::type::AssociatedCalendar* associated_calendar_for_week_end = new navitia::type::AssociatedCalendar();
        associated_calendar_for_week->calendar = week_cal;
        associated_calendar_for_week_end->calendar = weekend_cal;
        pt_data.associated_calendars.push_back(associated_calendar_for_week);
        pt_data.associated_calendars.push_back(associated_calendar_for_week_end);
        auto* week_mvj = pt_data.meta_vjs.get_mut("week");
        week_mvj->associated_calendars[week_cal->uri] = associated_calendar_for_week;
        auto* week_bis_mvj = pt_data.meta_vjs.get_mut("week_bis");
        week_bis_mvj->associated_calendars[week_cal->uri] = associated_calendar_for_week;
        auto* weekend_mvj = pt_data.meta_vjs.get_mut("weekend");
        weekend_mvj->associated_calendars[weekend_cal->uri] = associated_calendar_for_week_end;
        auto* all_mvj = pt_data.meta_vjs.get_mut("all");
        all_mvj->associated_calendars[week_cal->uri] = associated_calendar_for_week;
        all_mvj->associated_calendars[weekend_cal->uri] = associated_calendar_for_week_end;

        b.data->build_uri();
        b.data->complete();
        b.data->build_raptor();
        b.data->geo_ref->init();
    }
};
