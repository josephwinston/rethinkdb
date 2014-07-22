// Copyright 2010-2013 RethinkDB, all rights reserved.
#ifndef UNITTEST_BRANCH_HISTORY_MANAGER_HPP_
#define UNITTEST_BRANCH_HISTORY_MANAGER_HPP_

#include <set>

#include "clustering/immediate_consistency/branch/history.hpp"


namespace unittest {

class in_memory_branch_history_manager_t : public branch_history_manager_t {
public:
    in_memory_branch_history_manager_t() { }
    branch_birth_certificate_t get_branch(branch_id_t branch) THROWS_NOTHING;
    std::set<branch_id_t> known_branches() THROWS_NOTHING;
    void create_branch(branch_id_t branch_id, const branch_birth_certificate_t &bc, signal_t *interruptor) THROWS_ONLY(interrupted_exc_t);
    void export_branch_history(branch_id_t branch, branch_history_t *out) THROWS_NOTHING;
    void import_branch_history(const branch_history_t &new_records, signal_t *interruptor) THROWS_ONLY(interrupted_exc_t);

private:
    branch_history_t bh;

    DISABLE_COPYING(in_memory_branch_history_manager_t);
};

}  // namespace unittest

#endif  // UNITTEST_BRANCH_HISTORY_MANAGER_HPP_
