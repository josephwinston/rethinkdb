#include "clustering/immediate_consistency/query/direct_reader_metadata.hpp"
#include "clustering/immediate_consistency/query/master_metadata.hpp"

#include "containers/archive/stl_types.hpp"

RDB_IMPL_SERIALIZABLE_1_SINCE_v1_13(direct_reader_business_card_t, read_mailbox);
RDB_IMPL_EQUALITY_COMPARABLE_1(direct_reader_business_card_t, read_mailbox);

RDB_IMPL_SERIALIZABLE_4(
        master_business_card_t::read_request_t,
        read, order_token, fifo_token, cont_addr);
INSTANTIATE_SERIALIZABLE_FOR_CLUSTER(master_business_card_t::read_request_t);

RDB_IMPL_SERIALIZABLE_4_SINCE_v1_13(
        master_business_card_t::write_request_t, write, order_token, fifo_token, cont_addr);

RDB_IMPL_SERIALIZABLE_0_SINCE_v1_13(master_business_card_t::inner_client_business_card_t);

RDB_IMPL_SERIALIZABLE_2_SINCE_v1_13(master_business_card_t, region, multi_throttling);

RDB_IMPL_EQUALITY_COMPARABLE_2(master_business_card_t,
                               region, multi_throttling);
