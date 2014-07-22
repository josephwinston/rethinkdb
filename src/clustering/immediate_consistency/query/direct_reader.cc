// Copyright 2010-2014 RethinkDB, all rights reserved.
#include "clustering/immediate_consistency/query/direct_reader.hpp"

#include "protocol_api.hpp"
#include "store_view.hpp"

direct_reader_t::direct_reader_t(
        mailbox_manager_t *mm,
        store_view_t *svs_) :
    mailbox_manager(mm),
    svs(svs_),
    read_mailbox(mm, std::bind(&direct_reader_t::on_read, this,
                               ph::_1, ph::_2))
    { }

direct_reader_business_card_t direct_reader_t::get_business_card() {
    return direct_reader_business_card_t(read_mailbox.get_address());
}

void direct_reader_t::on_read(
        const read_t &read,
        const mailbox_addr_t<void(read_response_t)> &cont) {
    auto_drainer_t::lock_t keepalive(&drainer);

    try {
        read_token_pair_t token_pair;
        svs->new_read_token_pair(&token_pair);

#ifndef NDEBUG
        trivial_metainfo_checker_callback_t metainfo_checker_callback;
        metainfo_checker_t metainfo_checker(&metainfo_checker_callback, svs->get_region());
#endif

        read_response_t response;
        svs->read(DEBUG_ONLY(metainfo_checker, )
                  read,
                  &response,
                  order_source.check_in("direct_reader_t::perform_read").with_read_mode(),
                  &token_pair,
                  keepalive.get_drain_signal());
        send(mailbox_manager, cont, response);
    } catch (const interrupted_exc_t &) {
        /* ignore */
    }
}
