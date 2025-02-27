/* Copyright 2025 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See https://www.boost.org/libs/bloom for library home page.
 */

#include <boost/mp11/algorithm.hpp>
#include <vector>
#include "test_types.hpp"
#include "test_utilities.hpp"

using namespace test_utilities;

template<typename Filter,typename ValueFactory>
void test_combination()
{
  using filter=Filter;
  using value_type=typename filter::value_type;

  std::vector<value_type> input1,input2;
  ValueFactory            fac;
  for(int i=0;i<10;++i){
    input1.push_back(fac());
    input2.push_back(fac());
  }

  {
    filter f1{input1.begin(),input1.end(),1000},
           f1_copy{f1},
           f2{input2.begin(),input2.end(),f1.capacity()+1};

    BOOST_TEST_THROWS(f1&=filter{},std::invalid_argument);
    BOOST_TEST(f1==f1_copy);
    BOOST_TEST_THROWS(f1&=f2,std::invalid_argument);
    BOOST_TEST(f1==f1_copy);
    BOOST_TEST_THROWS(f1|=filter{},std::invalid_argument);
    BOOST_TEST(f1==f1_copy);
    BOOST_TEST_THROWS(f1|=f2;,std::invalid_argument);
    BOOST_TEST(f1==f1_copy);
  }
  {
    filter f1{input1.begin(),input1.end(),1000},
           f1_copy{f1},
           empty{f1.capacity()};

    f1|=empty;
    BOOST_TEST(f1==f1_copy);
    f1&=empty;
    BOOST_TEST(f1==empty);
  }
  {
    filter       f1{input1.begin(),input1.end(),1000};
    const filter f2{input2.begin(),input2.end(),f1.capacity()};

    f1.insert(input2.begin(),input2.end());
    f1&=f2;
    check_may_contain(f1,input2);
    check_may_not_contain(f1,input1);
  }
  {
    filter       f1{input1.begin(),input1.end(),1000};
    const filter f2{input2.begin(),input2.end(),f1.capacity()};

    f1|=f2;
    check_may_contain(f1,input1);
    check_may_contain(f1,input2);
  }
}

struct lambda
{
  template<typename T>
  void operator()(T)
  {
    using filter=typename T::type;
    using value_type=typename filter::value_type;

    test_combination<filter,value_factory<value_type>>();
  }
};

int main()
{
  boost::mp11::mp_for_each<identity_test_types>(lambda{});
  return boost::report_errors();
}
