/* Copyright 2025 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See https://www.boost.org/libs/bloom for library home page.
 */

#include <boost/core/lightweight_test.hpp>
#include <boost/mp11/algorithm.hpp>
#include <cmath>
#include <limits>
#include <new>
#include <string>
#include "test_types.hpp"
#include "test_utilities.hpp"

using namespace test_utilities;

template<typename T>
struct throwing_allocator
{
  using value_type=T;

  throwing_allocator()=default;
  template<typename U>
  throwing_allocator(const throwing_allocator<U>&){}

  T* allocate(std::size_t n)
  {
    using limits=std::numeric_limits<std::size_t>;
    static constexpr std::size_t alloc_limit=
      limits::digits>=64?(limits::max)():(limits::max)()/256;

    if(n>alloc_limit)throw std::bad_alloc{};
    return static_cast<T*>(::operator new(n*sizeof(T)));
  }

  void deallocate(T* p,std::size_t){::operator delete(p);}

  bool operator==(const throwing_allocator& x)const{return true;}
  bool operator!=(const throwing_allocator& x)const{return false;}
};

static constexpr std::size_t N=1000000;

template<typename Filter>
double measure_fpr(Filter&& f)
{
  using value_type=typename std::remove_reference<Filter>::type::value_type;

  value_factory<value_type> fac;
  std::size_t               res=0;
  for(int i=0;i<N;++i)f.insert(fac());
  for(int i=0;i<N;++i)res+=f.may_contain(fac());

  return (double)res/N;
}

template<typename Filter>
void test_fpr()
{
  using filter=rehash_filter<
    revalue_filter<
      realloc_filter<
        Filter,
        throwing_allocator<typename Filter::value_type>
      >,
      std::string
    >,
    boost::hash<std::string>
  >;

  BOOST_TEST_EQ(filter(0,0.01).capacity(),0);
  BOOST_TEST_THROWS((void)filter(1,0.0),std::bad_alloc);
  BOOST_TEST_EQ(filter(100,1.0).capacity(),0);

  {
    static constexpr int max_fpr_exp=
      std::numeric_limits<std::size_t>::digits>=64?5:3;

    for(int i=1;i<=max_fpr_exp;++i){
      double target_fpr=std::pow(10,(double)-i);
      double measured_fpr=measure_fpr(filter(N,target_fpr));
      double err=std::abs((double)measured_fpr-target_fpr)/target_fpr;
      BOOST_TEST_LT(err,0.75);
    }
  }
}

struct lambda
{
  template<typename T>
  void operator()(T)
  {
    using filter=typename T::type;

    test_fpr<filter>();
  }
};

int main()
{
  boost::mp11::mp_for_each<identity_test_types>(lambda{});
  return boost::report_errors();
}
