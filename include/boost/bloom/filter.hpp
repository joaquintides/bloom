/* Configurable Bloom filter.
 * 
 * Copyright 2025 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See https://www.boost.org/libs/bloom for library home page.
 */

#ifndef BOOST_BLOOM_FILTER_HPP
#define BOOST_BLOOM_FILTER_HPP

#include <boost/bloom/block.hpp>
#include <boost/bloom/detail/core.hpp>
#include <boost/bloom/detail/type_traits.hpp>
#include <boost/config.hpp>
#include <boost/container_hash/hash.hpp>
#include <boost/core/allocator_traits.hpp>
#include <boost/core/empty_value.hpp>
#include <boost/cstdint.hpp>
#include <boost/unordered/hash_traits.hpp> // TODO: internalize?
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

namespace boost{
namespace bloom{
namespace detail{

/* Mixing policies: no_mix_policy is the identity function, and
 * mulx_mix_policy uses the mulx_mix function from
 * <boost/bloom/detail/mulx.hpp>.
 *
 * filter mixes hash results with mulx64_mix if the hash is not marked as
 * avalanching, i.e. it's not of good quality (see
 * <boost/unordered/hash_traits.hpp>), or if std::size_t is less than 64 bits
 * (mixing policies promote to boost::uint64_t).
 */

struct no_mix_policy
{
  template<typename Hash,typename T>
  static inline boost::uint64_t mix(const Hash& h,const T& x)
  {
    return (boost::uint64_t)h(x);
  }
};

struct mulx64_mix_policy
{
  template<typename Hash,typename T>
  static inline boost::uint64_t mix(const Hash& h,const T& x)
  {
    return mulx64_mix((boost::uint64_t)h(x));
  }
};


template<typename Allocator,typename T>
class allocator_constructed
{
public:
  template<typename...Args>
  allocator_constructed(const Allocator& al_,Args&&... args):al{al_}
  {
    allocator_construct(al,std::addressof(u.x),std::forward<Args>(args)...);
  }

  ~allocator_constructed()
  {
    allocator_destroy(al,std::addressof(u.x));
  }

  const T& value()const noexcept{return u.x;}

private:
  union uninitialized_value
  {
    uninitialized_value(){}
    ~uninitialized_value(){}

    T x;
  };
  
  uninitialized_value u;
  Allocator          al;
};

} /* namespace detail */

#if defined(BOOST_MSVC)
#pragma warning(push)
#pragma warning(disable:4714) /* marked as __forceinline not inlined */
#endif

template<
  typename T,typename Hash,std::size_t K,
  typename Subfilter=block<unsigned char,1>,std::size_t BucketSize=0,
  typename Allocator=std::allocator<T>
>
class

#if defined(_MSC_VER)&&_MSC_FULL_VER>=190023918
__declspec(empty_bases) /* activate EBO with multiple inheritance */
#endif

filter:
  detail::filter_core<
    K,Subfilter,BucketSize,allocator_rebind_t<Allocator,unsigned char>
  >,
  empty_value<Hash,0>
{
  BOOST_BLOOM_STATIC_ASSERT_IS_CV_UNQUALIFIED_OBJECT(T);
  static_assert(
    std::is_same<T,allocator_value_type_t<Allocator>>::value,
    "Allocator's value_type must be T");
  using super=detail::filter_core<
    K,Subfilter,BucketSize,allocator_rebind_t<Allocator,unsigned char>
  >;
  using mix_policy=typename std::conditional<
    unordered::hash_is_avalanching<Hash>::value&&
    sizeof(std::size_t)>=sizeof(boost::uint64_t),
    detail::no_mix_policy,
    detail::mulx64_mix_policy
  >::type;

public:
  using value_type=T;
  using super::k;
  using hasher=Hash;
  using subfilter=typename super::subfilter;
  using allocator_type=Allocator;
  using size_type=typename super::size_type;
  using difference_type=typename super::difference_type;
  using reference=value_type&;
  using const_reference=const value_type&;
  using pointer=value_type*;
  using const_pointer=const value_type*;

  filter()=default;

  explicit filter(
    std::size_t m,const hasher& h=hasher(),
    const allocator_type& al=allocator_type()):
    super{m,al},hash_base{empty_init,h}{}

  template<typename InputIterator>
  filter(
    InputIterator first,InputIterator last,
    std::size_t m,const hasher& h=hasher(),
    const allocator_type& al=allocator_type()):
    filter{m,h,al}
  {
    insert(first,last);
  }

  filter(const filter&)=default;
  filter(filter&&)=default;

  template<typename InputIterator>
  filter(
    InputIterator first,InputIterator last,
    std::size_t m,const allocator_type& al):
    filter{first,last,m,hasher(),al}{}

  explicit filter(const allocator_type& al):filter{0,al}{}

  filter(const filter& x,const allocator_type& al):
    super{x,al},hash_base{empty_init,x.h()}{}

  filter(filter&& x,const allocator_type& al):
    super{std::move(x),al},hash_base{empty_init,std::move(x.h())}{}

  filter(
    std::initializer_list<value_type> il,
    std::size_t m,const hasher& h=hasher(),
    const allocator_type& al=allocator_type()):
    filter{il.begin(),il.end(),m,h,al}{}

  filter(std::size_t m,const allocator_type& al):
    filter{m,hasher(),al}{}

  filter(
    std::initializer_list<value_type> il,
    std::size_t m,const allocator_type& al):
    filter{il.begin(),il.end(),m,hasher(),al}{}

  filter& operator=(const filter& x)
  {
    BOOST_BLOOM_STATIC_ASSERT_IS_NOTHROW_SWAPPABLE(Hash);
    using std::swap;

    auto x_h=x.h();
    super::operator=(x);
    swap(h(),x_h);
    return *this;
  }

  filter& operator=(filter&& x)
    noexcept(noexcept(std::declval<super&>()==(std::declval<super&&>())))
  {
    BOOST_BLOOM_STATIC_ASSERT_IS_NOTHROW_SWAPPABLE(Hash);
    using std::swap;

    super::operator=(std::move(x));
    swap(h(),x.h());
    return *this;
  }

  filter& operator=(std::initializer_list<value_type> il)
  {
    clear();
    insert(il);
    return *this;
  }

  using super::get_allocator;
  using super::capacity;

  template<typename... Args>
  BOOST_FORCEINLINE void emplace(Args&&... args)
  {
    insert(detail::allocator_constructed<allocator_type,value_type>{
      get_allocator(),std::forward<Args>(args)...}.value());
  }

  template<
    typename U,
    std::enable_if<std::is_same<T,detail::remove_cvref_t<U>>::value>* =nullptr
  >
  BOOST_FORCEINLINE void emplace(U&& x)
  {
    insert(x); /* avoid value_type construction */
  }

  BOOST_FORCEINLINE void insert(const T& x)
  {
    super::insert(hash_for(x));
  }

  template<
    typename K,
    typename H=hasher,detail::enable_if_transparent_t<H>* =nullptr
  >
  BOOST_FORCEINLINE void insert(const K& x)
  {
    super::insert(hash_for(x));
  }

  template<typename InputIterator>
  void insert(InputIterator first,InputIterator last)
  {
    while(first!=last)emplace(*first++);
  }

  void insert(std::initializer_list<value_type> il)
  {
    insert(il.begin(),il.end());
  }

  void swap(filter& x)
    noexcept(noexcept(std::declval<super&>().swap(std::declval<super&>())))
  {
    BOOST_BLOOM_STATIC_ASSERT_IS_NOTHROW_SWAPPABLE(Hash);
    using std::swap;

    super::swap(x);
    swap(h(),x.h());
  }

  using super::clear;
  using super::reset;

  filter& operator&=(const filter& x)
  {
    super::operator&=(x);
    return *this;
  }

  filter& operator|=(const filter& x)
  {
    super::operator|=(x);
    return *this;
  }

  hasher hash_function()const
  {
    return h();
  }

  BOOST_FORCEINLINE bool may_contain(const T& x)const
  {
    return super::may_contain(hash_for(x));
  }

  template<
    typename K,
    typename H=hasher,detail::enable_if_transparent_t<H>* =nullptr
  >
  BOOST_FORCEINLINE bool may_contain(const K& x)const
  {
    return super::may_contain(hash_for(x));
  }

private:
  template<
    typename T1,typename H,std::size_t K1,typename S,std::size_t B,typename A
  >
  bool friend operator==(
    const filter<T1,H,K1,S,B,A>& x,const filter<T1,H,K1,S,B,A>& y);

  using hash_base=empty_value<Hash,0>;

  const Hash& h()const{return hash_base::get();}
  Hash& h(){return hash_base::get();}

  template<typename K>
  inline boost::uint64_t hash_for(const K& x)const
  {
    return mix_policy::mix(h(),x);
  }
};

template<
  typename T,typename H,std::size_t K,typename S,std::size_t B,typename A
>
bool operator==(const filter<T,H,K,S,B,A>& x,const filter<T,H,K,S,B,A>& y)
{
  using super=typename filter<T,H,K,S,B,A>::super;
  return static_cast<const super&>(x)==static_cast<const super&>(y);
}

template<
  typename T,typename H,std::size_t K,typename S,std::size_t B,typename A
>
bool operator!=(const filter<T,H,K,S,B,A>& x,const filter<T,H,K,S,B,A>& y)
{
  return !(x==y);
}

template<
  typename T,typename H,std::size_t K,typename S,std::size_t B,typename A
>
void swap(filter<T,H,K,S,B,A>& x,filter<T,H,K,S,B,A>& y)
  noexcept(noexcept(x.swap(y)))
{
  x.swap(y);
}

#if defined(BOOST_MSVC)
#pragma warning(pop) /* C4714 */
#endif

} /* namespace bloom */
} /* namespace boost */
#endif
