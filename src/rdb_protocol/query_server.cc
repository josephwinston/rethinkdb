// Copyright 2010-2013 RethinkDB, all rights reserved.
#include "rdb_protocol/query_server.hpp"

#include "concurrency/cross_thread_watchable.hpp"
#include "concurrency/watchable.hpp"
#include "rdb_protocol/counted_term.hpp"
#include "rdb_protocol/env.hpp"
#include "rdb_protocol/profile.hpp"
#include "rdb_protocol/stream_cache.hpp"
#include "rpc/semilattice/view/field.hpp"

rdb_query_server_t::rdb_query_server_t(const std::set<ip_address_t> &local_addresses,
                                       int port,
                                       rdb_context_t *_rdb_ctx) :
    server(_rdb_ctx, local_addresses, port, this, _rdb_ctx->auth_metadata),
    rdb_ctx(_rdb_ctx),
    thread_counters(0)
{

}

http_app_t *rdb_query_server_t::get_http_app() {
    return &server;
}

int rdb_query_server_t::get_port() const {
    return server.get_port();
}

// Predeclaration for run, only used here
namespace ql {
    void run(protob_t<Query> q,
             rdb_context_t *ctx,
             signal_t *interruptor,
             stream_cache_t *stream_cache,
             Response *response_out);
}

class scoped_ops_running_stat_t {
public:
    explicit scoped_ops_running_stat_t(perfmon_counter_t *_counter)
        : counter(_counter) {
        ++(*counter);
    }
    ~scoped_ops_running_stat_t() {
        --(*counter);
    }
private:
    perfmon_counter_t *counter;
    DISABLE_COPYING(scoped_ops_running_stat_t);
};

bool rdb_query_server_t::run_query(const ql::protob_t<Query> &query,
                                   Response *response_out,
                                   client_context_t *client_ctx) {
    guarantee(client_ctx->interruptor != NULL);
    response_out->set_token(query->token());

    counted_t<const ql::datum_t> noreply = static_optarg("noreply", query);
    bool response_needed = !(noreply.has() &&
         noreply->get_type() == ql::datum_t::type_t::R_BOOL &&
         noreply->as_bool());
    try {
        scoped_ops_running_stat_t stat(&rdb_ctx->ql_ops_running);
        guarantee(rdb_ctx->reql_admin_interface);
        // `ql::run` will set the status code
        ql::run(query,
                rdb_ctx,
                client_ctx->interruptor,
                &client_ctx->stream_cache,
                response_out);
    } catch (const ql::exc_t &e) {
        fill_error(response_out, Response::COMPILE_ERROR, e.what(), e.backtrace());
    } catch (const ql::datum_exc_t &e) {
        fill_error(response_out, Response::COMPILE_ERROR, e.what(), ql::backtrace_t());
    } catch (const interrupted_exc_t &e) {
        ql::fill_error(response_out, Response::RUNTIME_ERROR,
                       "Query interrupted.  Did you shut down the server?");
#ifdef NDEBUG // In debug mode we crash, in release we send an error.
    } catch (const std::exception &e) {
        ql::fill_error(response_out, Response::RUNTIME_ERROR,
                       strprintf("Unexpected exception: %s\n", e.what()));
#endif // NDEBUG
    }

    return response_needed;
}

void rdb_query_server_t::unparseable_query(int64_t token,
                                           Response *response_out,
                                           const std::string &info) {
    response_out->set_token(token);
    ql::fill_error(response_out, Response::CLIENT_ERROR, info);
}

