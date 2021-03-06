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

#pragma once

#include "crypto/crypto.h"
#include "cryptonote_core/cryptonote_basic.h"

#include "single_tx_test_base.h"

class test_derive_public_key : public single_tx_test_base
{
public:
  static const size_t loop_count = 1000;

  bool init()
  {
    if (!single_tx_test_base::init())
      return false;

    crypto::generate_key_derivation(m_tx_pub_key, m_bob.get_keys().m_view_secret_key, m_key_derivation);
    m_spend_public_key = m_bob.get_keys().m_account_address.m_spend_public_key;

    return true;
  }

  bool test()
  {
    cryptonote::keypair in_ephemeral;
    crypto::derive_public_key(m_key_derivation, 0, m_spend_public_key, in_ephemeral.pub);
    return true;
  }

private:
  crypto::key_derivation m_key_derivation;
  crypto::public_key m_spend_public_key;
};
