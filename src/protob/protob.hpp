// Copyright 2010-2014 RethinkDB, all rights reserved.
#ifndef PROTOB_PROTOB_HPP_
#define PROTOB_PROTOB_HPP_

#include <set>
#include <map>
#include <string>
#include <vector>

#include "errors.hpp"
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>

#include "arch/runtime/runtime.hpp"
#include "arch/timing.hpp"
#include "concurrency/auto_drainer.hpp"
#include "concurrency/cross_thread_signal.hpp"
#include "containers/archive/archive.hpp"
#include "http/http.hpp"

#include "rdb_protocol/stream_cache.hpp"
#include "rdb_protocol/counted_term.hpp"

class auth_key_t;
class auth_semilattice_metadata_t;
template <class> class semilattice_readwrite_view_t;

class client_context_t {
public:
    explicit client_context_t(rdb_context_t *rdb_ctx,
                              ql::reject_cfeeds_t reject_cfeeds,
                              signal_t *_interruptor)
        : interruptor(_interruptor),
          stream_cache(rdb_ctx, reject_cfeeds) { }
    // Holy shit, this field gets MODIFIED!
    signal_t *interruptor;
    ql::stream_cache_t stream_cache;
};

class http_conn_cache_t : public repeating_timer_callback_t {
public:
    class http_conn_t {
    public:
        explicit http_conn_t(rdb_context_t *rdb_ctx) :
            in_use(false),
            last_accessed(time(0)),
            client_ctx(rdb_ctx, ql::reject_cfeeds_t::YES, &interruptor) {
        }

        client_context_t *get_ctx() {
            last_accessed = time(0);
            return &client_ctx;
        }

        void pulse() {
            rassert(!interruptor.is_pulsed());
            interruptor.pulse();
        }

        bool is_expired() {
            return difftime(time(0), last_accessed) > TIMEOUT_SEC;
        }

        bool acquire() {
            if (in_use) {
                return false;
            }
            in_use = true;
            return true;
        }

        void release() {
            in_use = false;
        }

    private:
        bool in_use;
        cond_t interruptor;
        time_t last_accessed;
        client_context_t client_ctx;
        DISABLE_COPYING(http_conn_t);
    };

    http_conn_cache_t() : next_id(0), http_timeout_timer(TIMER_RESOLUTION_MS, this) { }
    ~http_conn_cache_t() {
        std::map<int32_t, boost::shared_ptr<http_conn_t> >::iterator it;
        for (it = cache.begin(); it != cache.end(); ++it) it->second->pulse();
    }

    boost::shared_ptr<http_conn_t> find(int32_t key) {
        std::map<int32_t, boost::shared_ptr<http_conn_t> >::iterator
            it = cache.find(key);
        if (it == cache.end()) return boost::shared_ptr<http_conn_t>();
        return it->second;
    }
    int32_t create(rdb_context_t *rdb_ctx) {
        int32_t key = next_id++;
        cache.insert(
            std::make_pair(key, boost::make_shared<http_conn_t>(rdb_ctx)));
        return key;
    }
    void erase(int32_t key) {
        auto it = cache.find(key);
        if (it != cache.end()) {
            it->second->pulse();
            cache.erase(it);
        }
    }

    void on_ring() {
        for (auto it = cache.begin(); it != cache.end();) {
            auto tmp = it++;
            if (tmp->second->is_expired()) {
                tmp->second->pulse();
                cache.erase(tmp);
            }
        }
    }
private:
    static const time_t TIMEOUT_SEC = 5*60;
    static const int64_t TIMER_RESOLUTION_MS = 5000;

    std::map<int32_t, boost::shared_ptr<http_conn_t> > cache;
    int32_t next_id;
    repeating_timer_t http_timeout_timer;
};

class query_handler_t {
public:
    virtual ~query_handler_t() { }

    virtual MUST_USE bool run_query(const ql::protob_t<Query> &query,
                                    Response *response_out,
                                    client_context_t *client_ctx) = 0;

    virtual void unparseable_query(int64_t token,
                                   Response *response_out,
                                   const std::string &info) = 0;
};

class query_server_t : public http_app_t {
public:
    query_server_t(rdb_context_t *rdb_ctx,
                   const std::set<ip_address_t> &local_addresses,
                   int port,
                   query_handler_t *_handler,
                   boost::shared_ptr<semilattice_readwrite_view_t<auth_semilattice_metadata_t> > _auth_metadata);
    ~query_server_t();

    int get_port() const;

private:
    static std::string read_sized_string(tcp_conn_t *conn,
                                         size_t max_size,
                                         const std::string &length_error_msg,
                                         signal_t *interruptor);
    static auth_key_t read_auth_key(tcp_conn_t *conn, signal_t *interruptor);

    // For the client driver socket
    void handle_conn(const scoped_ptr_t<tcp_conn_descriptor_t> &nconn,
                     auto_drainer_t::lock_t);

    // This is templatized based on the wire protocol requested by the client
    template<class protocol_t>
    void connection_loop(tcp_conn_t *conn,
                         client_context_t *client_ctx);

    // For HTTP server
    void handle(const http_req_t &request,
                http_res_t *result,
                signal_t *interruptor);

    rdb_context_t *const rdb_ctx;

    query_handler_t *const handler;

    boost::shared_ptr<semilattice_readwrite_view_t<auth_semilattice_metadata_t> >
        auth_metadata;

    /* WARNING: The order here is fragile. */
    cond_t main_shutting_down_cond;
    signal_t *shutdown_signal() {
        return shutting_down_conds[get_thread_id().threadnum].get();
    }

    std::vector<scoped_ptr_t<cross_thread_signal_t> > shutting_down_conds;

    auto_drainer_t auto_drainer;

    struct pulse_on_destruct_t {
        explicit pulse_on_destruct_t(cond_t *_cond) : cond(_cond) { }
        ~pulse_on_destruct_t() { cond->pulse(); }
        cond_t *cond;
    } pulse_sdc_on_shutdown;

    http_conn_cache_t http_conn_cache;

    scoped_ptr_t<tcp_listener_t> tcp_listener;

    int next_thread;
};

#endif /* PROTOB_PROTOB_HPP_ */
