// Copyright 2010-2013 RethinkDB, all rights reserved.
#ifndef CLUSTERING_ADMINISTRATION_HTTP_JSON_ADAPTERS_HPP_
#define CLUSTERING_ADMINISTRATION_HTTP_JSON_ADAPTERS_HPP_

#include <string>

#include "http/json/json_adapter.hpp"
#include "rpc/connectivity/connectivity.hpp"
#include "rpc/semilattice/joins/deletable.hpp"
#include "rpc/semilattice/joins/vclock.hpp"

/* There are a couple of semilattice structures that we need to have json
 * adaptable for the administration server, this code shouldn't go with the
 * definition of these structures because they're deep in the rpc directory
 * which generally doesn't concern itself with how people interact with its
 * structures. */

/* The vclock_t type needs a special apapter which allows it to be resolved.
 * When the vector clock goes in to conflict all of the other requests will
 * fail. */
template <class T>
class json_vclock_resolver_t : public json_adapter_if_t {
public:
    json_vclock_resolver_t(vclock_t<T> *, const vclock_ctx_t &ctx);

private:
    json_adapter_if_t::json_adapter_map_t get_subfields_impl();
    cJSON *render_impl();
    void apply_impl(cJSON *change);
    void reset_impl();
    void erase_impl();
    boost::shared_ptr<subfield_change_functor_t> get_change_callback();

    vclock_t<T> *target_;
    const vclock_ctx_t ctx_;

    DISABLE_COPYING(json_vclock_resolver_t);
};

template <class T>
class json_vclock_adapter_t : public json_adapter_if_t {
public:
    json_vclock_adapter_t(vclock_t<T> *, const vclock_ctx_t &ctx);

private:
    json_adapter_if_t::json_adapter_map_t get_subfields_impl();
    cJSON *render_impl();
    void apply_impl(cJSON *);
    void reset_impl();
    void erase_impl();
    boost::shared_ptr<subfield_change_functor_t>  get_change_callback();

    vclock_t<T> *target_;
    const vclock_ctx_t ctx_;

    DISABLE_COPYING(json_vclock_adapter_t);
};

//json adapter concept for vclock_t
template <class T>
json_adapter_if_t::json_adapter_map_t with_ctx_get_json_subfields(vclock_t<T> *, const vclock_ctx_t &);

template <class T>
cJSON *with_ctx_render_as_json(vclock_t<T> *, const vclock_ctx_t &);

//Note this is not actually part of the json_adapter concept but is a special
//purpose rendering function which is needed by the json_vclock_resolver_t
template <class T>
cJSON *with_ctx_render_all_values(vclock_t<T> *, const vclock_ctx_t &);

template <class T>
void with_ctx_apply_json_to(cJSON *, vclock_t<T> *, const vclock_ctx_t &);

template <class T>
void with_ctx_on_subfield_change(vclock_t<T> *, const vclock_ctx_t &);

//json adapter concept for deletable_t
template <class T, class ctx_t>
json_adapter_if_t::json_adapter_map_t with_ctx_get_json_subfields(deletable_t<T> *, const ctx_t &);

template <class T, class ctx_t>
cJSON *with_ctx_render_as_json(deletable_t<T> *, const ctx_t &);

template <class T, class ctx_t>
void with_ctx_apply_json_to(cJSON *, deletable_t<T> *, const ctx_t &);

template <class T, class ctx_t>
void with_ctx_erase_json(deletable_t<T> *, const ctx_t &);

template <class T, class ctx_t>
void with_ctx_on_subfield_change(deletable_t<T> *, const ctx_t &);

// ctx-less json adapter concept for deletable_t
template <class T>
json_adapter_if_t::json_adapter_map_t get_json_subfields(deletable_t<T> *);

template <class T>
cJSON *render_as_json(deletable_t<T> *);

template <class T>
void apply_json_to(cJSON *, deletable_t<T> *);

template <class T>
void erase_json(deletable_t<T> *);

// ctx-less json adapter concept for peer_id_t
json_adapter_if_t::json_adapter_map_t get_json_subfields(peer_id_t *);

cJSON *render_as_json(peer_id_t *);

void apply_json_to(cJSON *, peer_id_t *);


#include "clustering/administration/http/json_adapters.tcc"

#endif /* CLUSTERING_ADMINISTRATION_HTTP_JSON_ADAPTERS_HPP_ */
