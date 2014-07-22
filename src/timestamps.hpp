// Copyright 2010-2014 RethinkDB, all rights reserved.
#ifndef TIMESTAMPS_HPP_
#define TIMESTAMPS_HPP_

#include <inttypes.h>

#include "containers/archive/archive.hpp"
#include "repli_timestamp.hpp"
#include "rpc/serialize_macros.hpp"

class printf_buffer_t;


/* These are the timestamp types used by the clustering code.
`repli_timestamp_t`, which is used internally within the btree code, is defined
elsewhere. */

/* `state_timestamp_t` is a unique identifier of a particular point in a
timeline. `transition_timestamp_t` is the unique identifier of a transition from
one `state_timestamp_t` to the next. Databases have `state_timestamp_t`s, and
transactions have `transition_timestamp_t`s. */

class state_timestamp_t {
public:
    bool operator==(state_timestamp_t t) const { return num == t.num; }
    bool operator!=(state_timestamp_t t) const { return num != t.num; }
    bool operator<(state_timestamp_t t) const { return num < t.num; }
    bool operator>(state_timestamp_t t) const { return num > t.num; }
    bool operator<=(state_timestamp_t t) const { return num <= t.num; }
    bool operator>=(state_timestamp_t t) const { return num >= t.num; }

    static state_timestamp_t zero() {
        state_timestamp_t t;
        t.num = 0;
        return t;
    }

    // TODO get rid of this. This is only for a hack until we know what to do with timestamps
    repli_timestamp_t to_repli_timestamp() const {
        repli_timestamp_t ts;
        ts.longtime = num;
        return ts;
    }

    friend void debug_print(printf_buffer_t *buf, state_timestamp_t ts);

    RDB_MAKE_ME_SERIALIZABLE_1(num);

private:
    friend class transition_timestamp_t;
    uint64_t num;
};

RDB_SERIALIZE_OUTSIDE(state_timestamp_t);

void debug_print(printf_buffer_t *buf, state_timestamp_t ts);

class transition_timestamp_t {
public:
    bool operator==(transition_timestamp_t t) const { return before == t.before; }
    bool operator!=(transition_timestamp_t t) const { return before != t.before; }
    bool operator<(transition_timestamp_t t) const { return before < t.before; }
    bool operator>(transition_timestamp_t t) const { return before > t.before; }
    bool operator<=(transition_timestamp_t t) const { return before <= t.before; }
    bool operator>=(transition_timestamp_t t) const { return before >= t.before; }

    static transition_timestamp_t starting_from(state_timestamp_t before) {
        transition_timestamp_t t;
        t.before = before;
        return t;
    }

    state_timestamp_t timestamp_before() const {
        return before;
    }

    state_timestamp_t timestamp_after() const {
        state_timestamp_t after;
        after.num = before.num + 1;
        guarantee(after > before, "timestamp counter overflowed");
        return after;
    }

    // TODO get rid of this. This is only for a hack until we know what to do with timestamps
    repli_timestamp_t to_repli_timestamp() const {
        return before.to_repli_timestamp();
    }

    RDB_MAKE_ME_SERIALIZABLE_1(before);

private:
    state_timestamp_t before;
};

RDB_SERIALIZE_OUTSIDE(transition_timestamp_t);


#endif /* TIMESTAMPS_HPP_ */
