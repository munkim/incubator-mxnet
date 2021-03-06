/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 *  \file memory_test.cc
 *  \brief Perf/profile run of ActivationOp
 *  \author Chris Olivier
 */

#include <gtest/gtest.h>
#include <dmlc/omp.h>
#include <mxnet/tensor_blob.h>
#include "../include/test_util.h"
#include "../include/test_perf.h"

using namespace mxnet;

#ifdef _OPENMP
template<typename Container>
static typename Container::value_type average(const Container& cont) {
  typename Container::value_type avg = 0;
  const size_t sz = cont.size();
  for (auto iter = cont.begin(), e_iter = cont.end(); iter != e_iter; ++iter) {
    avg += *iter / sz;  // Attempt to not overflow by dividing up incrementally
  }
  return avg;
}

static std::string pretty_num(uint64_t val) {
  std::string res, s = std::to_string(val);
  size_t ctr = 0;
  for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i, ++ctr) {
    if (ctr && (ctr % 3) == 0) {
      res += ",";
    }
    res.push_back(s[i]);
  }
  std::reverse(res.begin(), res.end());
  return res;
}

static int GetOMPThreadCount() {
  return omp_get_max_threads() >> 1;
}

/*!
 * \brief Generic bidirectional sanity test
 */
TEST(MEMORY_TEST, MemsetAndMemcopyPerformance) {
  const size_t GB = 1000000000;  // memset never slower
  uint64_t base = 100000;
  std::list<uint64_t> memset_times, omp_set_times, memcpy_times, omp_copy_times;
  size_t pass = 0;
  do {
    memset_times.resize(0);
    omp_set_times.resize(0);
    memcpy_times.resize(0);
    omp_copy_times.resize(0);;

    const size_t test_size = 2 * base;
    std::cout << "====================================" << std::endl
              << "Data size: " << pretty_num(test_size) << std::endl << std::flush;

    std::unique_ptr<uint8_t> buffer_1(new uint8_t[test_size]), buffer_2(new uint8_t[test_size]);
    uint8_t *src = buffer_1.get(), *dest = buffer_2.get();

    for (size_t x = 0; x < 5; ++x) {
      // Init memory with different values
      memset(src, 3, test_size);
      memset(dest, 255, test_size);  // wipe out some/all of src cache

      // memset
      uint64_t start = test::perf::getNannoTickCount();
      memset(src, 123, test_size);
      const uint64_t memset_time = test::perf::getNannoTickCount() - start;

      start = test::perf::getNannoTickCount();
      #pragma omp parallel for num_threads(GetOMPThreadCount())
      for (int i = 0; i < test_size; ++i) {
        src[i] = 42;
      }
      const uint64_t omp_set_time = test::perf::getNannoTickCount() - start;

      start = test::perf::getNannoTickCount();
      memcpy(dest, src, test_size);
      const uint64_t memcpy_time = test::perf::getNannoTickCount() - start;

      // bounce the cache and dirty logic
      memset(src, 6, test_size);
      memset(dest, 200, test_size);

      start = test::perf::getNannoTickCount();
      #pragma omp parallel for num_threads(GetOMPThreadCount())
      for (int i = 0; i < test_size; ++i) {
        dest[i] = src[i];
      }
      const uint64_t omp_copy_time = test::perf::getNannoTickCount() - start;

      memset_times.push_back(memset_time);
      omp_set_times.push_back(omp_set_time);
      memcpy_times.push_back(memcpy_time);
      omp_copy_times.push_back(omp_copy_time);

      std::cout << "memset time:   " << pretty_num(memcpy_time) << " ns" << std::endl
                << "omp set time:  " << pretty_num(omp_set_time) << " ns" << std::endl
                << std::endl;
      std::cout << "memcpy time:   " << pretty_num(memcpy_time) << " ns" << std::endl
                << "omp copy time: " << pretty_num(omp_copy_time) << " ns" << std::endl
                << std::endl;
    }
    std::cout << "------------------------------------" << std::endl;
    if (average(memset_times) > average(omp_set_times)) {
      std::cout << "<< MEMSET SLOWER FOR " << pretty_num(test_size) << " items >>" << std::endl;
    }
    if (average(memcpy_times) > average(omp_copy_times)) {
      std::cout << "<< MEMCPY SLOWER FOR " << pretty_num(test_size) << " items >>" << std::endl;
    }
    if (!pass) {
      GTEST_ASSERT_LE(average(memset_times), average(omp_set_times));
      GTEST_ASSERT_LE(average(memcpy_times), average(omp_copy_times));
    }
    base *= 10;
    ++pass;
  } while (test::performance_run
           && base <= GB
           && (average(memset_times) < average(omp_set_times)
               || average(memcpy_times), average(omp_copy_times)));
}
#endif  // _OPENMP
