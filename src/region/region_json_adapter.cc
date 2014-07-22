// Copyright 2010-2014 RethinkDB, all rights reserved.
#include "region/region_json_adapter.hpp"

#include <string>

#include "http/http.hpp"
#include "http/json.hpp"
#include "region/region.hpp"

// json adapter concept for `store_key_t`
json_adapter_if_t::json_adapter_map_t get_json_subfields(store_key_t *) {
    return json_adapter_if_t::json_adapter_map_t();
}

cJSON *render_as_json(store_key_t *target) {
    return cJSON_CreateString(percent_escaped_string(key_to_unescaped_str(*target)).c_str());
}

void apply_json_to(cJSON *change, store_key_t *target) {
    std::string str;
    if (!percent_unescape_string(get_string(change), &str)) {
        throw schema_mismatch_exc_t(strprintf("Failed to parse %s as a store_key_t.\n", get_string(change).c_str()));
    }

    if (!unescaped_str_to_key(str.c_str(), str.length(), target)) {
        throw schema_mismatch_exc_t(strprintf("Failed to parse %s as a store_key_t.\n", get_string(change).c_str()));
    }
}

std::string to_string_for_json_key(const store_key_t *target) {
    return percent_escaped_string(key_to_unescaped_str(*target));
}

// json adapter for key_range_t
json_adapter_if_t::json_adapter_map_t get_json_subfields(key_range_t *) {
    return json_adapter_if_t::json_adapter_map_t();
}

std::string render_region_as_string(key_range_t *target) {
    scoped_cJSON_t res(cJSON_CreateArray());

    res.AddItemToArray(render_as_json(&target->left));

    if (!target->right.unbounded) {
        res.AddItemToArray(render_as_json(&target->right.key));
    } else {
        res.AddItemToArray(cJSON_CreateNull());
    }

    return res.PrintUnformatted();
}

cJSON *render_as_json(key_range_t *target) {
    return cJSON_CreateString(render_region_as_string(target).c_str());
}

void apply_json_to(cJSON *change, key_range_t *target) {
    // TODO: Can we so casually call get_string on a cJSON object?  What if it's not a string?
    const std::string change_str = get_string(change);
    scoped_cJSON_t js(cJSON_Parse(change_str.c_str()));
    if (js.get() == NULL) {
        throw schema_mismatch_exc_t(strprintf("Failed to parse %s as a key_range_t.", change_str.c_str()));
    }

    /* TODO: If something other than an array is passed here, then it will crash
    rather than report the error to the user. */
    json_array_iterator_t it(js.get());

    cJSON *first = it.next();
    if (first == NULL) {
        throw schema_mismatch_exc_t(strprintf("Failed to parse %s as a key_range_t.", change_str.c_str()));
    }

    cJSON *second = it.next();
    if (second == NULL) {
        throw schema_mismatch_exc_t(strprintf("Failed to parse %s as a key_range_t.", change_str.c_str()));
    }

    if (it.next() != NULL) {
        throw schema_mismatch_exc_t(strprintf("Failed to parse %s as a key_range_t.", change_str.c_str()));
    }

    store_key_t left;
    apply_json_to(first, &left);
    if (second->type == cJSON_NULL) {
        *target = key_range_t(key_range_t::closed, left,
                              key_range_t::none, store_key_t());
    } else {
        store_key_t right;
        apply_json_to(second, &right);

        if (left > right) {
            throw schema_mismatch_exc_t(strprintf("Failed to parse %s as a key_range_t -- bounds are out of order", change_str.c_str()));
        }

        *target = key_range_t(key_range_t::closed, left,
                              key_range_t::open,   right);
    }
}




// json adapter for hash_region_t<key_range_t>

// TODO: This is extremely ghetto: we assert that the hash region isn't split by hash value (because why should the UI ever be exposed to that?) and then only serialize the key range.
json_adapter_if_t::json_adapter_map_t get_json_subfields(UNUSED hash_region_t<key_range_t> *target) {
    return json_adapter_if_t::json_adapter_map_t();
}

std::string render_region_as_string(hash_region_t<key_range_t> *target) {
    // TODO: ghetto low level hash_region_t assertion.
    // TODO: Reintroduce this ghetto low level hash_region_t assertion.
    // guarantee(target->beg == 0 && target->end == HASH_REGION_HASH_SIZE);

    return render_region_as_string(&target->inner);
}

std::string to_string_for_json_key(hash_region_t<key_range_t> *target) {
    return render_region_as_string(target);
}

cJSON *render_as_json(hash_region_t<key_range_t> *target) {
    return cJSON_CreateString(render_region_as_string(target).c_str());
}

void apply_json_to(cJSON *change, hash_region_t<key_range_t> *target) {
    apply_json_to(change, &target->inner);
    target->beg = 0;
    target->end = target->inner.is_empty() ? 0 : HASH_REGION_HASH_SIZE;
}

