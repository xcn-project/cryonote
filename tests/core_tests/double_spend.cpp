// Copyright (c) 2018, The CryoNote Developers.
// Portions Copyright (c) 2012-2013, The CryptoNote Developers.
//
// All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "chaingen.h"
#include "chaingen_tests_list.h"

using namespace epee;
using namespace cryptonote;


//======================================================================================================================

gen_double_spend_in_different_chains::gen_double_spend_in_different_chains()
{
  REGISTER_CALLBACK_METHOD(gen_double_spend_in_different_chains, check_double_spend);
}

bool gen_double_spend_in_different_chains::generate(std::vector<test_event_entry>& events) const
{
  INIT_DOUBLE_SPEND_TEST();

  SET_EVENT_VISITOR_SETT(events, event_visitor_settings::set_txs_keeped_by_block, true);
  MAKE_TX(events, tx_1, bob_account, alice_account, send_amount / 2 - TESTS_DEFAULT_FEE, blk_1);
  events.pop_back();
  MAKE_TX(events, tx_2, bob_account, alice_account, send_amount - TESTS_DEFAULT_FEE, blk_1);
  events.pop_back();

  // Main chain
  events.push_back(tx_1);
  MAKE_NEXT_BLOCK_TX1(events, blk_2, blk_1r, miner_account, tx_1);

  // Alternative chain
  events.push_back(tx_2);
  MAKE_NEXT_BLOCK_TX1(events, blk_3, blk_1r, miner_account, tx_2);
  // Switch to alternative chain
  MAKE_NEXT_BLOCK(events, blk_4, blk_3, miner_account);
  CHECK_AND_NO_ASSERT_MES(expected_blockchain_height == get_block_height(blk_4) + 1, false, "expected_blockchain_height has invalid value");

  DO_CALLBACK(events, "check_double_spend");

  return true;
}

bool gen_double_spend_in_different_chains::check_double_spend(cryptonote::core& c, size_t /*ev_index*/, const std::vector<test_event_entry>& events)
{
  DEFINE_TESTS_ERROR_CONTEXT("gen_double_spend_in_different_chains::check_double_spend");

  std::list<block> block_list;
  bool r = c.get_blocks(0, 100 + 2 * CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW, block_list);
  CHECK_TEST_CONDITION(r);

  std::vector<block> blocks(block_list.begin(), block_list.end());
  CHECK_EQ(expected_blockchain_height, blocks.size());

  CHECK_EQ(1, c.get_pool_transactions_count());
  CHECK_EQ(1, c.get_alternative_blocks_count());

  cryptonote::account_base bob_account = boost::get<cryptonote::account_base>(events[1]);
  cryptonote::account_base alice_account = boost::get<cryptonote::account_base>(events[2]);

  std::vector<cryptonote::block> chain;
  map_hash2tx_t mtx;
  r = find_block_chain(events, chain, mtx, get_block_hash(blocks.back()));
  CHECK_TEST_CONDITION(r);
  CHECK_EQ(0, get_balance(bob_account, blocks, mtx));
  CHECK_EQ(send_amount - TESTS_DEFAULT_FEE, get_balance(alice_account, blocks, mtx));

  return true;
}
