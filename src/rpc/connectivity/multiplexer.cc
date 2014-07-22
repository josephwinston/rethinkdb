// Copyright 2010-2012 RethinkDB, all rights reserved.
#include "rpc/connectivity/multiplexer.hpp"

#include "errors.hpp"
#include <boost/bind.hpp>
#include <boost/function.hpp>

#include "rpc/connectivity/connectivity.hpp"


message_multiplexer_t::run_t::run_t(message_multiplexer_t *p) : parent(p) {
    guarantee(parent->run == NULL);
    parent->run = this;
#ifndef NDEBUG
    for (int i = 0; i < max_tag; i++) {
        if (parent->clients[i]) {
            rassert(parent->clients[i]->run);
        }
    }
#endif  // NDEBUG
}

message_multiplexer_t::run_t::~run_t() {
    guarantee(parent->run == this);
    parent->run = NULL;
}

void message_multiplexer_t::run_t::on_message(peer_id_t source,
                                              cluster_version_t version,
                                              read_stream_t *stream) {
    // All cluster versions currently use the same kinds of tags.
    tag_t tag;
    static_assert(std::is_same<tag_t, uint8_t>::value,
                  "tag_t is no longer uint8_t -- the format of cluster messages has "
                  "changed and you need to ask whether live cluster upgrades work.");
    archive_result_t res = deserialize_universal(stream, &tag);
    if (bad(res)) { throw fake_archive_exc_t(); }
    client_t *client = parent->clients[tag];
    guarantee(client != NULL, "Got a message for an unfamiliar tag. Apparently "
        "we aren't compatible with the cluster on the other end.");
    client->run->message_handler->on_message(source, version, stream);
}

message_multiplexer_t::client_t::run_t::run_t(client_t *c, message_handler_t *m) :
    parent(c), message_handler(m)
{
    guarantee(parent->parent->run == NULL);
    guarantee(parent->run == NULL);
    parent->run = this;
}

message_multiplexer_t::client_t::run_t::~run_t() {
    guarantee(parent->parent->run == NULL);
    guarantee(parent->run == this);
    parent->run = NULL;
}

message_multiplexer_t::client_t::client_t(message_multiplexer_t *p,
                                          tag_t t,
                                          int max_outstanding) :
    parent(p),
    tag(t),
    run(NULL),
    outstanding_writes_semaphores(max_outstanding)
{
    guarantee(parent->run == NULL);
    guarantee(parent->clients[tag] == NULL);
    parent->clients[tag] = this;
}

message_multiplexer_t::client_t::~client_t() {
    guarantee(parent->run == NULL);
    guarantee(parent->clients[tag] == this);
    parent->clients[tag] = NULL;
}

connectivity_service_t *message_multiplexer_t::client_t::get_connectivity_service() {
    return parent->message_service->get_connectivity_service();
}

class tagged_message_writer_t : public send_message_write_callback_t {
public:
    tagged_message_writer_t(message_multiplexer_t::tag_t _tag, send_message_write_callback_t *_subwriter) :
        tag(_tag), subwriter(_subwriter) { }
    virtual ~tagged_message_writer_t() { }

    void write(cluster_version_t cluster_version, write_stream_t *os) {
        // All cluster versions use a uint8_t tag here.
        write_message_t wm;
        static_assert(std::is_same<decltype(tag), message_multiplexer_t::tag_t>::value
                      && std::is_same<message_multiplexer_t::tag_t, uint8_t>::value,
                      "We expect to be serializing a uint8_t -- if this has changed, "
                      "the cluster communication format has changed and you need to "
                      "ask yourself whether live cluster upgrades work.");
        serialize_universal(&wm, tag);
        int res = send_write_message(os, &wm);
        if (res) { throw fake_archive_exc_t(); }
        subwriter->write(cluster_version, os);
    }

private:
    message_multiplexer_t::tag_t tag;
    send_message_write_callback_t *subwriter;
};

void message_multiplexer_t::client_t::send_message(peer_id_t dest, send_message_write_callback_t *callback) {
    tagged_message_writer_t writer(tag, callback);
    {
        semaphore_acq_t outstanding_write_acq (outstanding_writes_semaphores.get());
        parent->message_service->send_message(dest, &writer);
        // Release outstanding_writes_semaphore
    }
}

void message_multiplexer_t::client_t::kill_connection(peer_id_t peer) {
    parent->message_service->kill_connection(peer);
}

message_multiplexer_t::message_multiplexer_t(message_service_t *super_ms) :
    message_service(super_ms), run(NULL)
{
    for (int i = 0; i < max_tag; i++) {
        clients[i] = NULL;
    }
}

message_multiplexer_t::~message_multiplexer_t() {
    guarantee(run == NULL);
    for (int i = 0; i < max_tag; i++) {
        guarantee(clients[i] == NULL);
    }
}
