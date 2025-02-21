/* Copyright 2025 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See https://www.boost.org/libs/bloom for library home page.
 */

#include <boost/core/lightweight_test.hpp>
#include <boost/mp11/algorithm.hpp>
#include <new>
#include <utility>
#include <vector>
#include "test_types.hpp"
#include "test_utilities.hpp"

using namespace test_utilities;

template<typename Functor>
struct stateful:Functor
{
  stateful(int state_=0):state{state_}{}
  stateful(const stateful&)=default;
  stateful(stateful&& x)noexcept:
    Functor{std::move(x)},state{x.state}{x.state=-1;}

  stateful& operator=(stateful&& x)noexcept
  {
    if(this!=&x){
      Functor::operator=(std::move(x));
      state=x.state;
      x.state=-1;
    }
    return *this;
  }

  int state;
};

template<
  typename T,
  typename Propagate=std::false_type,typename AlwaysEqual=std::false_type
>
struct stateful_allocator
{
  using value_type=T;
  using propagate_on_container_copy_assignment=Propagate;
  using propagate_on_container_move_assignment=Propagate;
  using propagate_on_container_swap=Propagate;
  using is_always_equal=AlwaysEqual;

  stateful_allocator(int state_=0):state{state_}{}

  template<typename U>
  stateful_allocator(const stateful_allocator<U>& x):
    state{x.state},last_allocation{x.last_allocation}{}

  T* allocate(std::size_t n)
  {
    last_allocation=static_cast<T*>(::operator new(n*sizeof(T)));
    return last_allocation;
  }

  void deallocate(T* p,std::size_t){::operator delete(p);}

  bool operator==(const stateful_allocator& x)const
  {
    return AlwaysEqual::value||(state==x.state);
  }

  bool operator!=(const stateful_allocator& x)const{return !(*this==x);}

  int state;
  T*  last_allocation=nullptr;
};

template<typename Filter,typename Hash>
struct rehash_filter_impl;

template<
  typename T,typename H,std::size_t K,typename S,std::size_t B,typename A,
  typename Hash
>
struct rehash_filter_impl<boost::bloom::filter<T,H,K,S,B,A>,Hash>
{
  using type=boost::bloom::filter<T,Hash,K,S,B,A>;
};

template<typename Filter,typename Hash>
using rehash_filter=typename rehash_filter_impl<Filter,Hash>::type;

template<typename Filter,typename Allocator>
struct realloc_filter_impl;

template<
  typename T,typename H,std::size_t K,typename S,std::size_t B,typename A,
  typename Allocator
>
struct realloc_filter_impl<boost::bloom::filter<T,H,K,S,B,A>,Allocator>
{
  using type=boost::bloom::filter<T,H,K,S,B,Allocator>;
};

template<typename Filter,typename Allocator>
using realloc_filter=typename realloc_filter_impl<Filter,Allocator>::type;

template<typename Filter,typename Input>
void insert(Filter& f,const Input& input)
{
  for(const auto& x:input)f.insert(x);
}

template<typename Filter,typename Input>
void check_may_contain(const Filter& f,const Input& input)
{
  std::size_t res=0;
  for(const auto& x:input)res+=f.may_contain(x);
  BOOST_TEST_EQ(res,input.size());
}

template<
  typename Filter,typename ValueFactory,typename Propagate,typename AlwaysEqual
>
void test_pocxx()
{
  static constexpr auto propagate=Propagate::value;
  static constexpr auto always_equal=AlwaysEqual::value;
  using filter=realloc_filter<
    rehash_filter<Filter,stateful<typename Filter::hasher>>,
    stateful_allocator<unsigned char,Propagate,AlwaysEqual>
  >;
  using value_type=typename filter::value_type;
  using hasher=typename filter::hasher;
  using allocator_type=typename filter::allocator_type;

  std::vector<value_type> input;
  ValueFactory            fac;
  for(int i=0;i<10;++i)input.push_back(fac());

  {
    filter f1(input.begin(),input.end(),1000,hasher{42},allocator_type{2025});
    filter f2(allocator_type{1492});
    f2=f1;
    BOOST_TEST_GE(f1.capacity(),1000);
    BOOST_TEST_EQ(f1.hash_function().state,42);
    BOOST_TEST_EQ(f1.get_allocator().state,2025);
    BOOST_TEST_GE(f2.capacity(),1000);
    BOOST_TEST_EQ(f2.hash_function().state,42);
    BOOST_TEST_EQ(f2.get_allocator().state,propagate?2025:1492);
    check_may_contain(f2,input);
  }
  {
    filter f1(input.begin(),input.end(),1000,hasher{42},allocator_type{2025});
    auto p1=f1.get_allocator().last_allocation;
    filter f2(0,hasher{24},allocator_type{1492});
    f2=std::move(f1);
    BOOST_TEST_EQ(f1.capacity(),0);
    BOOST_TEST_EQ(f1.hash_function().state,24);
    BOOST_TEST_EQ(f1.get_allocator().state,2025);
    BOOST_TEST_GE(f2.capacity(),1000);
    BOOST_TEST_EQ(f2.hash_function().state,42);
    BOOST_TEST_EQ(f2.get_allocator().state,propagate?2025:1492);
    if(propagate){
      BOOST_TEST_EQ(f2.get_allocator().last_allocation,p1);
    }
    else if(always_equal){
      BOOST_TEST_EQ(f2.get_allocator().last_allocation,nullptr);
    }
    else{
      BOOST_TEST_NE(f2.get_allocator().last_allocation,nullptr);
      BOOST_TEST_NE(f2.get_allocator().last_allocation,p1);
    }
    check_may_contain(f2,input);
  }
  if(propagate||always_equal){
    filter f1(input.begin(),input.end(),1000,hasher{42},allocator_type{2025});
    auto p1=f1.get_allocator().last_allocation;
    filter f2(0,hasher{24},allocator_type{1492});
    if(propagate){ /* just a way to test member and non-member swap */
      f2.swap(f1);
    }
    else{
      using std::swap;
      swap(f1,f2);
    }
    BOOST_TEST_EQ(f1.capacity(),0);
    BOOST_TEST_EQ(f1.hash_function().state,24);
    BOOST_TEST_EQ(f1.get_allocator().state,propagate?1492:2025);
    BOOST_TEST_EQ(f1.get_allocator().last_allocation,propagate?nullptr:p1);
    BOOST_TEST_GE(f2.capacity(),1000);
    BOOST_TEST_EQ(f2.hash_function().state,42);
    BOOST_TEST_EQ(f2.get_allocator().state,propagate?2025:1492);
    BOOST_TEST_EQ(f2.get_allocator().last_allocation,propagate?p1:nullptr);
  }
}

template<typename Filter,typename ValueFactory>
void test_construction()
{
  using filter=realloc_filter<
    rehash_filter<Filter,stateful<typename Filter::hasher>>,
    stateful_allocator<unsigned char>
  >;
  using value_type=typename filter::value_type;
  using hasher=typename filter::hasher;
  using allocator_type=typename filter::allocator_type;

  std::vector<value_type> input;
  ValueFactory            fac;
  for(int i=0;i<10;++i)input.push_back(fac());

  std::initializer_list<value_type> il={input[0],input[1],input[2],input[3]};

  {
    filter f;
    BOOST_TEST_EQ(f.capacity(),0);
    BOOST_TEST_EQ(f.hash_function().state,0);
    BOOST_TEST_EQ(f.get_allocator().state,0);
  }
  {
    filter f(1000);
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,0);
    BOOST_TEST_EQ(f.get_allocator().state,0);
  }
  {
    filter f(1000,hasher{42});
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,42);
    BOOST_TEST_EQ(f.get_allocator().state,0);
  }
  {
    filter f(1000,hasher{42},allocator_type{2025});
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,42);
    BOOST_TEST_EQ(f.get_allocator().state,2025);
  }
  {
    filter f(input.begin(),input.end(),1000);
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,0);
    BOOST_TEST_EQ(f.get_allocator().state,0);
    check_may_contain(f,input);
  }
  {
    filter f(input.begin(),input.end(),1000,hasher{42});
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,42);
    BOOST_TEST_EQ(f.get_allocator().state,0);
    check_may_contain(f,input);
  }
  {
    filter f(input.begin(),input.end(),1000,hasher{42},allocator_type{2025});
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,42);
    BOOST_TEST_EQ(f.get_allocator().state,2025);
    check_may_contain(f,input);
  }
  {
    filter f1(1000,hasher{42},allocator_type{2025});
    insert(f1,input);
    filter f2(f1);
    BOOST_TEST_GE(f1.capacity(),1000);
    BOOST_TEST_EQ(f1.hash_function().state,42);
    BOOST_TEST_EQ(f1.get_allocator().state,2025);
    BOOST_TEST_EQ(f2.capacity(),f1.capacity());
    BOOST_TEST_EQ(f2.hash_function().state,42);
    BOOST_TEST_EQ(f2.get_allocator().state,2025);
    check_may_contain(f2,input);
  }
  {
    filter f1(1000,hasher{42},allocator_type{2025});
    insert(f1,input);
    auto p1=f1.get_allocator().last_allocation;
    filter f2(std::move(f1));
    BOOST_TEST_EQ(f1.capacity(),0);
    BOOST_TEST_EQ(f1.hash_function().state,-1);
    BOOST_TEST_GE(f2.capacity(),1000);
    BOOST_TEST_EQ(f2.hash_function().state,42);
    BOOST_TEST_EQ(f2.get_allocator().state,2025);
    BOOST_TEST_EQ(f2.get_allocator().last_allocation,p1);
    check_may_contain(f2,input);
  }
  {
    filter f(input.begin(),input.end(),1000,allocator_type{2025});
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,0);
    BOOST_TEST_EQ(f.get_allocator().state,2025);
    check_may_contain(f,input);
  }
  {
    filter f(allocator_type{2025});
    BOOST_TEST_EQ(f.capacity(),0);
    BOOST_TEST_EQ(f.hash_function().state,0);
    BOOST_TEST_EQ(f.get_allocator().state,2025);
  }
  {
    filter f1(1000,hasher{42},allocator_type{2025});
    insert(f1,input);
    filter f2(f1,allocator_type{1492});
    BOOST_TEST_GE(f1.capacity(),1000);
    BOOST_TEST_EQ(f1.hash_function().state,42);
    BOOST_TEST_EQ(f1.get_allocator().state,2025);
    check_may_contain(f1,input);
    BOOST_TEST_EQ(f2.capacity(),f1.capacity());
    BOOST_TEST_EQ(f2.hash_function().state,42);
    BOOST_TEST_EQ(f2.get_allocator().state,1492);
    check_may_contain(f2,input);
  }
  {
    filter f1(1000,hasher{42},allocator_type{2025});
    insert(f1,input);
    auto p1=f1.get_allocator().last_allocation;
    filter f2(std::move(f1),allocator_type{1492});
    BOOST_TEST_EQ(f1.capacity(),0);
    BOOST_TEST_EQ(f1.hash_function().state,-1);
    BOOST_TEST_EQ(f1.get_allocator().state,2025);
    BOOST_TEST_GE(f2.capacity(),1000);
    BOOST_TEST_EQ(f2.hash_function().state,42);
    BOOST_TEST_EQ(f2.get_allocator().state,1492);
    BOOST_TEST_NE(f2.get_allocator().last_allocation,nullptr);
    BOOST_TEST_NE(f2.get_allocator().last_allocation,p1);
    check_may_contain(f2,input);

    filter f3(1000,hasher{42},allocator_type{2025});
    insert(f3,input);
    filter f4(std::move(f3),allocator_type{2025});
    BOOST_TEST_EQ(f3.capacity(),0);
    BOOST_TEST_EQ(f3.hash_function().state,-1);
    BOOST_TEST_EQ(f3.get_allocator().state,2025);
    BOOST_TEST_GE(f4.capacity(),1000);
    BOOST_TEST_EQ(f4.hash_function().state,42);
    BOOST_TEST_EQ(f4.get_allocator().state,2025);
    BOOST_TEST_EQ(f4.get_allocator().last_allocation,nullptr);
    check_may_contain(f4,input);
  }
  {
    filter f(il,1000);
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,0);
    BOOST_TEST_EQ(f.get_allocator().state,0);
    check_may_contain(f,il);
  }
  {
    filter f(il,1000,hasher{42});
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,42);
    BOOST_TEST_EQ(f.get_allocator().state,0);
    check_may_contain(f,il);
  }
  {
    filter f(il,1000,hasher{42},allocator_type{2025});
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,42);
    BOOST_TEST_EQ(f.get_allocator().state,2025);
    check_may_contain(f,il);
  }
  {
    filter f(1000,allocator_type{2025});
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,0);
    BOOST_TEST_EQ(f.get_allocator().state,2025);
  }
  {
    filter f(il,1000,allocator_type{2025});
    BOOST_TEST_GE(f.capacity(),1000);
    BOOST_TEST_EQ(f.hash_function().state,0);
    BOOST_TEST_EQ(f.get_allocator().state,2025);
    check_may_contain(f,il);
  }
  test_pocxx<Filter,ValueFactory,std::false_type,std::false_type>();
  test_pocxx<Filter,ValueFactory,std::false_type,std::true_type >();
  test_pocxx<Filter,ValueFactory,std::true_type, std::false_type>();
  test_pocxx<Filter,ValueFactory,std::true_type, std::true_type >();
  {
    std::vector<value_type> partial_input(input.begin()+il.size(),input.end());
    filter f(partial_input.begin(),partial_input.end(),1000);
    check_may_contain(f,partial_input);
    f=il;
    check_may_contain(f,il);
    std::size_t tn=0;
    for(const auto& x:partial_input)tn+=f.may_contain(x);
    BOOST_TEST_LT(tn,partial_input.size()); /* should be 0 with high prob. */
  }
}

struct lambda
{
  template<typename T>
  void operator()(T)
  {
    using filter=typename T::type;
    using value_type=typename filter::value_type;

    test_construction<filter,value_factory<value_type>>();
  }
};

int main()
{
  boost::mp11::mp_for_each<identity_test_types>(lambda{});
  return boost::report_errors();
}
