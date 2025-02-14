# Candidate Boost Bloom Library

(Candidate) Boost.Bloom provides the class template `boost::bloom::filter` that
can be configured to implement a classical [Bloom filter](https://en.wikipedia.org/wiki/Bloom_filter)
as well as variations discussed in the literature such as _blocked_ filters, _split block_/_multi-block_
filters, and more.

## Example

```cpp
#include <boost/bloom/filter.hpp>
#include <cassert>
#include <string>

int main()
{
  // Bloom filter of strings with 5 bits set per insertion
  using filter = boost::bloom::filter<std::string, boost::hash<std::string>, 5>;

  // create filter with a capacity of 1'000'000 **bits**
  filter f(1'000'000);

  // insert elements (they can't be erased, Bloom filters are insert-only)
  f.insert("hello");
  f.insert("Boost");
  //...

  // elements inserted are always correctly checked as such
  assert(f.may_contain("hello") == true);

  // elements not inserted may incorrectly be identified as such with a
  // false positive rate (FPR) which is a function of the array capacity,
  // the number of bits set per element and generally how the boost::bloom::filter
  // was specified
  if(f.may_contain("bye")) { // likely false
    //...
  }
}
```

## Filter definition

A `boost::bloom::filter` can be regarded as an array of _buckets_ selected pseudo-randomly
(based on a hash function) upon insertion: each of the buckets is passed to a _subfilter_
that marks one or more of its bits according to some associated strategy.

```cpp
template<
  typename T, typename Hash, std::size_t K,
  typename Subfilter = block<unsigned char, 1>, std::size_t BucketSize = 0,
  typename Allocator = std::allocator<unsigned char>  
>
class filter;
```

* `T`: type of the elements inserted.
* `Hash`: a hash function for `T`.
* `K` number of buckets marked per insertion.
* `Subfilter`: type of subfilter used (more on this later).
* `BucketSize`: the number of buckets is just the capacity of the underlying
array (in bytes), divided by `BucketSize`. When `BucketSize` is specified as zero,
the value `sizeof(Subfilter::value_type)` is used.

The default configuration with `block<unsigned char,1>` corresponds to a
classical Bloom filter setting K bits per elements uniformly distributed across
the array.

### Overlapping buckets

`BucketSize` can be any value other (and typically less) than `sizeof(Subfilter::value_type)`.
When this is the case, subfilters in adjacent buckets act on overlapping byte ranges.
Far from being a problem, this improves the resulting _false positive rate_ (FPR) of
the filter. The downside is that buckets won't be in general properly aligned in memory,
which may result in more cache misses.

## Provided subfilters

### `block<Block, K'>`

Sets `K'` bits in an underlying value of the unsigned integral type `Block`
(e.g. `unsigned char`, `uint32_t`, `uint64_t`). So,
a `filter<T, Hash, K, block<Block, K'>>` will set `K*K'` bits per element.
The tradeoff here is that insertion/lookup will be (much) faster than
with `filter<T, Hash, K*K'>` while the FPR will be worse (larger).
FPR is better the wider `Block` is.

### `multiblock<Block, K'>`

Instead of setting `K'` bits in a `Block` value, this subfilter sets
one bit on each of the elements of a `Block[K']` subarray. This improves FPR
but impacts performance with respect to `block<Block, K'>`, among other
things because cacheline boundaries can be crossed when accessing the subarray.

### `fast_multiblock32<K'>`

Statistically equivalent to `multiblock<uint32_t, K'>`, but uses a much
faster SIMD-based algorithm when AVX2 is available. This algorithm is
pretty much the same as that of [Apache Kudu](https://kudu.apache.org/)
[`BlockBloomFilter`](https://github.com/apache/kudu/blob/master/src/kudu/util/block_bloom_filter_avx2.cc),
but that implementation is fixed to `K'` = 8 whereas we accept
any value between 1 and 8.

## Estimating FPR

For a classical Bloom filter, the theoretical false positive rate, under some simplifying assumptions,
is given by

$$FPR(n,m,k)=\left(1 - \left(1 - \frac{1}{m}\right)^{kn}\right)^k \approx \left(1 - e^{-kn/m}\right)^k \text{   for large } m,$$

where $$n$$ is the number of elements inserted in the filter, $$m$$ its capacity in bits and $$k$$ the
number of bits set per insertion (see a [derivation](https://en.wikipedia.org/wiki/Bloom_filter#Probability_of_false_positives)
of this formula). For a given inverse load factor $$c=m/n$$, the optimum $$k$$ is
the integer closest to:

$$k_{\text{opt}}=c\cdot\ln2,$$

yielding a minimum attainable FPR of $$1/2^{k_{\text{opt}}} \approx 0.6185^{c}$$.

In the case of a Boost.Bloom block filter of the form `filter<T, Hash, K, block<Block, K'>>`, we can extend
the approach from [Putze et al.](https://citeseerx.ist.psu.edu/document?repid=rep1&type=pdf&doi=f376ff09a64b388bfcde2f5353e9ddb44033aac8)
to derive the (approximate but very precise) formula:

$$FPR_{\text{block}}(n,m,b,k,k')=\left(\sum_{i=0}^{\infty} \text{Pois}(i,nbk/m) \cdot FPR(i,b,k')\right)^{k},$$

where

$$\text{Pois}(i,\lambda)=\frac{\lambda^i e^{-\lambda}}{i!}$$

is the probability mass function of a [Poisson distribution](https://en.wikipedia.org/wiki/Poisson_distribution)
with mean $$\lambda$$, and $$b$$ is the size of `Block` in bits. If we're using `multiblock<Block,K'>`, we have

$$FPR_\text{multiblock}(n,m,b,k,k')=\left(\sum_{i=0}^{\infty} \text{Pois}(i,nbkk'/m) \cdot FPR(i,b,1)^{k'}\right)^{k}.$$

As we have commented before, in general 

$$FPR_\text{block}(n,m,b,k,k') \geq FPR_\text{multiblock}(n,m,b,k,k') \geq FPR(n,m,kk'),$$

that is, block and multi-block filters have worse FPR than the classical filter for the same number of bits
set per insertion, but they will be much faster. We have the particular case

$$FPR_{\text{block}}(n,m,b,k,1)=FPR_{\text{multiblock}}(n,m,b,k,1)=FPR(n,m,k),$$

which follows simply from the fact that using `{block|multiblock}<Block, 1>` behaves exactly as
a classical Bloom filter.

We don't know of any closed, simple formula for the FPR of block and multiblock filters when
`Bucketsize` is not its "natural" size (`sizeof(Block)` for `block<Block, K'>`,
`K'*sizeof(Block)` for `multiblock<Block, K'>`), that is, when subfilter values overlap,
but empirical calculations show that $$FPR/FPR_\text{baseline}$$ improves (reduces) with smaller values of `BucketSize`
and larger values of $$k$$, $$k'$$ and $$c=m/n$$. Some examples:

* `filter<T, Hash, 1, multiblock<unsigned char, 9>, BucketSize>`:
$$\frac{FPR(c=12,\texttt{BucketSize}=1)}{FPR(c=12,\texttt{BucketSize}=0)}=0.65$$
* `filter<T, Hash, 1, multiblock<uint32_t, 9>, BucketSize>`: 
$$\frac{FPR(c=12,\texttt{BucketSize}=1)}{FPR(c=12,\texttt{BucketSize}=0)}=0.80$$
* `filter<T, Hash, 1, multiblock<uint64_t, 9>, BucketSize>`: 
$$\frac{FPR(c=12,\texttt{BucketSize}=1)}{FPR(c=12,\texttt{BucketSize}=0)}=0.87$$

(Remember that `BucketSize` = 0 selects the non-overlapping case.)

## Experimental results

All benchmarks run on a Windows 10 machine with 8GB RAM and an Intel Core i5-8265U CPU @1.60GHz
(base frequency) using Clang 13.0.1 for Visual Studio (clang-cl), `/arch:AVX2`,
release mode (see [benchmarking code](benchmark/comparison_table.cpp)).

The tables show the FPR and execution times in nanoseconds per operation 
for three different configurations of `boost::bloom::filter<int, boost::hash<int>, ...>`.
The number of inserted elements is `N` = 10 million. Filters are constructed with a capacity
`c*N` (bits), so `c` is the number of bits used per element. For each combination of `c` and
a given filter configuration, the optimum value of `K` (that yielding the minimum FPR)
has been used.

For reference, we provide also insertion, successful lookup and unsuccessful lookup times
for a `boost::unordered_flat_set<int>` with `N` = 10 million elements.
This container uses 63.75 bits per element.

### 64-bit mode

<table>
  <tr><th colspan="3"><code>boost::unordered_flat_set</code></tr>
  <tr>
    <th>insertion</th>
    <th>successful<br/>lookup</th>
    <th>unsuccessful<br/>lookup</th>
  </tr>
  <tr>
    <td align="right">81.78</td>
    <td align="right">32.78</td>
    <td align="right">16.29</td>
  </tr>
</table>
<table>
  <tr>
    <th></th>
    <th colspan="5"><code>filter&lt;K></code></th>
    <th colspan="5"><code>filter&lt;1,block&lt;uint64_t,K>></code></th>
    <th colspan="5"><code>filter&lt;1,multiblock&lt;uint64_t,K>></code></th>
  </tr>
  <tr>
    <th>c</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
  </tr>
  <tr>
    <td align="center">8</td>
    <td align="center">6</td>
    <td align="right">2.1566</td>
    <td align="right">62.91</td>
    <td align="right">43.83</td>
    <td align="right">29.88</td>
    <td align="center">4</td>
    <td align="right">3.3462</td>
    <td align="right">13.49</td>
    <td align="right">10.45</td>
    <td align="right">10.29</td>
    <td align="center">5</td>
    <td align="right">2.4515</td>
    <td align="right">15.47</td>
    <td align="right">13.92</td>
    <td align="right">13.90</td>
  </tr>
  <tr>
    <td align="center">12</td>
    <td align="center">9</td>
    <td align="right">0.3146</td>
    <td align="right">114.18</td>
    <td align="right">68.75</td>
    <td align="right">36.60</td>
    <td align="center">5</td>
    <td align="right">1.0310</td>
    <td align="right">17.89</td>
    <td align="right">12.70</td>
    <td align="right">12.45</td>
    <td align="center">8</td>
    <td align="right">0.4244</td>
    <td align="right">16.65</td>
    <td align="right">14.19</td>
    <td align="right">14.51</td>
  </tr>
  <tr>
    <td align="center">16</td>
    <td align="center">11</td>
    <td align="right">0.0456</td>
    <td align="right">157.34</td>
    <td align="right">93.70</td>
    <td align="right">38.43</td>
    <td align="center">6</td>
    <td align="right">0.4035</td>
    <td align="right">19.64</td>
    <td align="right">15.18</td>
    <td align="right">14.61</td>
    <td align="center">11</td>
    <td align="right">0.0776</td>
    <td align="right">33.42</td>
    <td align="right">24.63</td>
    <td align="right">24.53</td>
  </tr>
  <tr>
    <td align="center">20</td>
    <td align="center">14</td>
    <td align="right">0.0066</td>
    <td align="right">212.80</td>
    <td align="right">127.66</td>
    <td align="right">40.25</td>
    <td align="center">7</td>
    <td align="right">0.1879</td>
    <td align="right">20.96</td>
    <td align="right">15.02</td>
    <td align="right">15.04</td>
    <td align="center">14</td>
    <td align="right">0.0153</td>
    <td align="right">37.16</td>
    <td align="right">30.01</td>
    <td align="right">29.95</td>
  </tr>
  <tr>
    <th></th>
    <th colspan="5"><code>filter&lt;K,fast_multiblock32&ltK>></code></th>
    <th colspan="5"><code>filter&lt;1,block&lt;uint64_t, K>,1></code></th>
    <th colspan="5"><code>filter&lt;1,multiblock&lt;uint64_t,K>,1></code></th>
  </tr>
  <tr>
    <th>c</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
  </tr>
  <tr>
    <td align="center">8</td>
    <td align="center">5</td>
    <td align="right">2.7367</td>
    <td align="right">15.58</td>
    <td align="right">12.13</td>
    <td align="right">12.25</td>
    <td align="center">5</td>
    <td align="right">3.0580</td>
    <td align="right">14.82</td>
    <td align="right">11.47</td>
    <td align="right">11.44</td>
    <td align="center">5</td>
    <td align="right">2.3256</td>
    <td align="right">15.58</td>
    <td align="right">12.98</td>
    <td align="right">12.94</td>
  </tr>
  <tr>
    <td align="center">12</td>
    <td align="center">8</td>
    <td align="right">0.5399</td>
    <td align="right">14.68</td>
    <td align="right">10.84</td>
    <td align="right">11.78</td>
    <td align="center">6</td>
    <td align="right">0.8253</td>
    <td align="right">19.41</td>
    <td align="right">14.57</td>
    <td align="right">14.67</td>
    <td align="center">8</td>
    <td align="right">0.3777</td>
    <td align="right">22.60</td>
    <td align="right">16.45</td>
    <td align="right">15.94</td>
  </tr>
  <tr>
    <td align="center">16</td>
    <td align="center">8</td>
    <td align="right">0.1314</td>
    <td align="right">17.81</td>
    <td align="right">12.00</td>
    <td align="right">11.98</td>
    <td align="center">7</td>
    <td align="right">0.2891</td>
    <td align="right">23.33</td>
    <td align="right">16.53</td>
    <td align="right">16.41</td>
    <td align="center">11</td>
    <td align="right">0.0643</td>
    <td align="right">34.14</td>
    <td align="right">24.44</td>
    <td align="right">24.43</td>
  </tr>
  <tr>
    <td align="center">20</td>
    <td align="center">8</td>
    <td align="right">0.0421</td>
    <td align="right">17.22</td>
    <td align="right">12.06</td>
    <td align="right">12.07</td>
    <td align="center">8</td>
    <td align="right">0.1213</td>
    <td align="right">23.56</td>
    <td align="right">16.17</td>
    <td align="right">16.05</td>
    <td align="center">14</td>
    <td align="right">0.0119</td>
    <td align="right">38.18</td>
    <td align="right">30.82</td>
    <td align="right">31.75</td>
  </tr>
</table>

### 32-bit mode

<table>
  <tr><th colspan="3"><code>boost::unordered_flat_set</code></tr>
  <tr>
    <th>insertion</th>
    <th>successful<br/>lookup</th>
    <th>unsuccessful<br/>lookup</th>
  </tr>
  <tr>
    <td align="right">87.61</td>
    <td align="right">38.44</td>
    <td align="right">17.42</td>
  </tr>
</table>
<table>
  <tr>
    <th></th>
    <th colspan="5"><code>filter&lt;K></code></th>
    <th colspan="5"><code>filter&lt;1,block&lt;uint64_t,K>></code></th>
    <th colspan="5"><code>filter&lt;1,multiblock&lt;uint64_t,K>></code></th>
  </tr>
  <tr>
    <th>c</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
  </tr>
  <tr>
    <td align="center">8</td>
    <td align="center">6</td>
    <td align="right">2.1489</td>
    <td align="right">80.30</td>
    <td align="right">77.15</td>
    <td align="right">49.44</td>
    <td align="center">4</td>
    <td align="right">3.3441</td>
    <td align="right">31.90</td>
    <td align="right">19.27</td>
    <td align="right">19.25</td>
    <td align="center">5</td>
    <td align="right">2.4525</td>
    <td align="right">23.87</td>
    <td align="right">22.40</td>
    <td align="right">22.37</td>
  </tr>
  <tr>
    <td align="center">12</td>
    <td align="center">9</td>
    <td align="right">0.3184</td>
    <td align="right">141.30</td>
    <td align="right">134.50</td>
    <td align="right">59.77</td>
    <td align="center">5</td>
    <td align="right">1.0317</td>
    <td align="right">38.74</td>
    <td align="right">27.25</td>
    <td align="right">27.16</td>
    <td align="center">8</td>
    <td align="right">0.4209</td>
    <td align="right">23.50</td>
    <td align="right">28.41</td>
    <td align="right">28.25</td>
  </tr>
  <tr>
    <td align="center">16</td>
    <td align="center">11</td>
    <td align="right">0.0455</td>
    <td align="right">191.88</td>
    <td align="right">176.28</td>
    <td align="right">59.54</td>
    <td align="center">6</td>
    <td align="right">0.4015</td>
    <td align="right">43.23</td>
    <td align="right">29.18</td>
    <td align="right">29.29</td>
    <td align="center">11</td>
    <td align="right">0.0783</td>
    <td align="right">53.02</td>
    <td align="right">49.14</td>
    <td align="right">49.17</td>
  </tr>
  <tr>
    <td align="center">20</td>
    <td align="center">14</td>
    <td align="right">0.0068</td>
    <td align="right">258.90</td>
    <td align="right">234.95</td>
    <td align="right">66.02</td>
    <td align="center">7</td>
    <td align="right">0.1883</td>
    <td align="right">60.00</td>
    <td align="right">34.54</td>
    <td align="right">34.65</td>
    <td align="center">14</td>
    <td align="right">0.0158</td>
    <td align="right">55.13</td>
    <td align="right">54.57</td>
    <td align="right">55.58</td>
  </tr>
  <tr>
    <th></th>
    <th colspan="5"><code>filter&lt;K,fast_multiblock32&ltK>></code></th>
    <th colspan="5"><code>filter&lt;1,block&lt;uint64_t, K>,1></code></th>
    <th colspan="5"><code>filter&lt;1,multiblock&lt;uint64_t,K>,1></code></th>
  </tr>
  <tr>
    <th>c</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
    <th>K</th>
    <th>FPR<br/>[%]</th>
    <th>ins.</th>
    <th>succ.<br/>lkp.</th>
    <th>uns.<br/>lkp.</th>
  </tr>
  <tr>
    <td align="center">8</td>
    <td align="center">5</td>
    <td align="right">2.7383</td>
    <td align="right">24.26</td>
    <td align="right">22.28</td>
    <td align="right">22.21</td>
    <td align="center">5</td>
    <td align="right">3.0456</td>
    <td align="right">34.83</td>
    <td align="right">24.33</td>
    <td align="right">24.40</td>
    <td align="center">5</td>
    <td align="right">2.3196</td>
    <td align="right">22.68</td>
    <td align="right">23.26</td>
    <td align="right">23.50</td>
  </tr>
  <tr>
    <td align="center">12</td>
    <td align="center">8</td>
    <td align="right">0.5398</td>
    <td align="right">19.13</td>
    <td align="right">21.53</td>
    <td align="right">21.52</td>
    <td align="center">6</td>
    <td align="right">0.8314</td>
    <td align="right">41.93</td>
    <td align="right">28.82</td>
    <td align="right">29.42</td>
    <td align="center">8</td>
    <td align="right">0.3752</td>
    <td align="right">28.13</td>
    <td align="right">31.14</td>
    <td align="right">31.12</td>
  </tr>
  <tr>
    <td align="center">16</td>
    <td align="center">8</td>
    <td align="right">0.1306</td>
    <td align="right">21.45</td>
    <td align="right">23.59</td>
    <td align="right">23.31</td>
    <td align="center">7</td>
    <td align="right">0.2897</td>
    <td align="right">45.76</td>
    <td align="right">34.49</td>
    <td align="right">33.96</td>
    <td align="center">11</td>
    <td align="right">0.0629</td>
    <td align="right">52.56</td>
    <td align="right">48.63</td>
    <td align="right">48.61</td>
  </tr>
  <tr>
    <td align="center">20</td>
    <td align="center">8</td>
    <td align="right">0.0416</td>
    <td align="right">21.69</td>
    <td align="right">23.81</td>
    <td align="right">23.65</td>
    <td align="center">8</td>
    <td align="right">0.1216</td>
    <td align="right">48.39</td>
    <td align="right">31.43</td>
    <td align="right">31.45</td>
    <td align="center">14</td>
    <td align="right">0.0118</td>
    <td align="right">55.20</td>
    <td align="right">55.86</td>
    <td align="right">55.82</td>
  </tr>
</table>
