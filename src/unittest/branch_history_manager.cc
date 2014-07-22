// Copyright 2010-2013 RethinkDB, all rights reserved.
#include "unittest/branch_history_manager.hpp"

#include <map>
#include <set>

#include "arch/timing.hpp"
#include "rdb_protocol/protocol.hpp"

namespace unittest {

branch_birth_certificate_t in_memory_branch_history_manager_t::get_branch(branch_id_t branch) THROWS_NOTHING {
    std::map<branch_id_t, branch_birth_certificate_t>::const_iterator it = bh.branches.find(branch);
    rassert(it != bh.branches.end(), "no such branch");
    return it->second;
}

std::set<branch_id_t> in_memory_branch_history_manager_t::known_branches() THROWS_NOTHING {
    std::set<branch_id_t> res;

    for (std::map<branch_id_t, branch_birth_certificate_t>::iterator it  = bh.branches.begin();
         it != bh.branches.end();
         ++it) {
        res.insert(it->first);
    }

    return res;
}

void in_memory_branch_history_manager_t::create_branch(branch_id_t branch_id, const branch_birth_certificate_t &bc, signal_t *interruptor) THROWS_ONLY(interrupted_exc_t) {
    rassert(bh.branches.find(branch_id) == bh.branches.end());
    nap(10, interruptor);
    bh.branches[branch_id] = bc;
}

void in_memory_branch_history_manager_t::export_branch_history(branch_id_t branch, branch_history_t *out) THROWS_NOTHING {
    rassert(!branch.is_nil());
    std::set<branch_id_t> to_process;
    if (out->branches.count(branch) == 0) {
        to_process.insert(branch);
    }
    while (!to_process.empty()) {
        branch_id_t next = *to_process.begin();
        to_process.erase(next);
        branch_birth_certificate_t bc = get_branch(next);
        rassert(out->branches.count(next) == 0);
        out->branches[next] = bc;
        for (region_map_t<version_range_t>::const_iterator it = bc.origin.begin(); it != bc.origin.end(); it++) {
            if (!it->second.latest.branch.is_nil() && out->branches.count(it->second.latest.branch) == 0) {
                to_process.insert(it->second.latest.branch);
            }
        }
    }
}

void in_memory_branch_history_manager_t::import_branch_history(const branch_history_t &new_records, signal_t *interruptor) THROWS_ONLY(interrupted_exc_t) {
    nap(10, interruptor);
    for (std::map<branch_id_t, branch_birth_certificate_t>::const_iterator it = new_records.branches.begin(); it != new_records.branches.end(); it++) {
        bh.branches.insert(std::make_pair(it->first, it->second));
    }
}

}  // namespace unittest
