/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010-2014 Joel Andersson, Joris Gillis, Moritz Diehl,
 *                            K.U. Leuven. All rights reserved.
 *    Copyright (C) 2011-2014 Greg Horn
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "casadi_runtime.hpp"
#include "../std_vector_tools.hpp"
#include <vector>
#include <map>

#ifndef CASADI_CASADI_SHARED_HPP
#define CASADI_CASADI_SHARED_HPP

namespace casadi {
template<typename T>
int einstein_process(const T& A, const T& B, const T& C,
  const std::vector<int>& dim_a, const std::vector<int>& dim_b, const std::vector<int>& dim_c,
  const std::vector<int>& a, const std::vector<int>& b, const std::vector<int>& c,
  std::vector<int>& iter_dims,
  std::vector<int>& strides_a, std::vector<int>& strides_b, std::vector<int>& strides_c
) {

    casadi_assert(A.is_vector() && A.is_dense());
    casadi_assert(B.is_vector() && B.is_dense());
    casadi_assert(C.is_vector() && C.is_dense());

    // Dimension check
    casadi_assert(A.numel()==product(dim_a));
    casadi_assert(B.numel()==product(dim_b));
    casadi_assert(C.numel()==product(dim_c));

    casadi_assert(dim_a.size()==a.size());
    casadi_assert(dim_b.size()==b.size());

    casadi_assert(c.size()<=a.size()+b.size());

    std::map<int, int> dim_map;

    // Check if shared nodes dimensions match up
    for (int i=0;i<a.size();++i) {
      int ai = a[i];
      if (ai>=0) continue;
      auto al = dim_map.find(ai);
      if (al==dim_map.end()) {
        dim_map[ai] = dim_a[i];
      } else {
        casadi_assert(al->second==dim_a[i]);
      }
    }

    for (int i=0;i<b.size();++i) {
      int bi = b[i];
      if (bi>=0) continue;
      auto bl = dim_map.find(bi);
      if (bl==dim_map.end()) {
        dim_map[bi] = dim_b[i];
      } else {
        casadi_assert(bl->second==dim_b[i]);
      }
    }

    for (int i=0;i<c.size();++i) {
      int ci = c[i];
      if (ci>=0) continue;
      auto cl = dim_map.find(ci);
      if (cl==dim_map.end()) {
        dim_map[ci] = dim_c[i];
      } else {
        casadi_assert(cl->second==dim_c[i]);
      }
    }

    std::vector< std::pair<int, int> > dim_map_pair;
    for (const auto & i : dim_map) dim_map_pair.push_back(i);

    std::sort(dim_map_pair.begin(), dim_map_pair.end(),
      [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.second < b.second;});

    std::vector<int> dim_map_keys;
    // Compute the total number of iterations needed
    int n_iter = 1;
    for (const auto& e : dim_map_pair) {
      n_iter*= e.second;
      dim_map_keys.push_back(-e.first);
      iter_dims.push_back(e.second);
    }

    strides_a.clear();
    strides_a.resize(iter_dims.size()+1);
    strides_b.clear();
    strides_b.resize(iter_dims.size()+1);
    strides_c.clear();
    strides_c.resize(iter_dims.size()+1);

    std::vector<int> lu;

    if (!dim_map_keys.empty()) lu = lookupvector(dim_map_keys);

    // Update data pointers to match indices
    int cumprod = 1;
    for (int j=0;j<a.size();++j) {
      if (a[j]<0) {
        strides_a[1+lu[-a[j]]] = cumprod;
      } else {
        strides_a[0]+=a[j]*cumprod;
      }
      cumprod*= dim_a[j];
    }
    cumprod = 1;
    for (int j=0;j<b.size();++j) {
      if (b[j]<0) {
        strides_b[1+lu[-b[j]]] = cumprod;
      } else {
        strides_b[0]+=b[j]*cumprod;
      }
      cumprod*= dim_b[j];
    }
    cumprod = 1;
    for (int j=0;j<c.size();++j) {
      if (c[j]<0) {
        strides_c[1+lu[-c[j]]] = cumprod;
      } else {
        strides_c[0]+=c[j]*cumprod;
      }
      cumprod*= dim_c[j];
    }

    return n_iter;
  }

  template<typename T>
  void Contraction(const T&a, const T& b, T& r) {
    r+= a*b;
  }

  template<> inline
  void Contraction(const bvec_t&a, const bvec_t& b, bvec_t& r) {
    r|= a | b;
  }

  template<typename T>
  void einstein_eval(int n_iter,
      const std::vector<int>& iter_dims,
      const std::vector<int>& strides_a, const std::vector<int>& strides_b, const std::vector<int>& strides_c,
      const T* a_in, const T* b_in, T* c_in) {

    if (!n_iter) return;

    int iter_dim1 = 1, iter_dim2 = 1, iter_dim3 = 1;

    int n = iter_dims.size();

    int stridea1=0, strideb1=0, stridec1=0;
    int stridea2=0, strideb2=0, stridec2=0;
    int stridea3=0, strideb3=0, stridec3=0;
    if (n>0) {
      iter_dim3 = iter_dims[n-1];
      stridea3 = strides_a[n];
      strideb3 = strides_b[n];
      stridec3 = strides_c[n];
    }
    if (n>1) {
      iter_dim2 = iter_dims[n-2];
      stridea2 = strides_a[n-1];
      strideb2 = strides_b[n-1];
      stridec2 = strides_c[n-1];
    }
    if (n>2) {
      iter_dim1 = iter_dims[n-3];
      stridea1 = strides_a[n-2];
      strideb1 = strides_b[n-2];
      stridec1 = strides_c[n-2];
    }


    const int* ptr_iter_dims = get_ptr(iter_dims);

    const int *ptr_strides_a = get_ptr(strides_a)+1;
    const int *ptr_strides_b = get_ptr(strides_b)+1;
    const int *ptr_strides_c = get_ptr(strides_c)+1;

    // Data pointers
    const T* a_perm = a_in+strides_a[0];
    const T* b_perm = b_in+strides_b[0];
    T* c_perm = c_in+strides_c[0];

    n_iter/= iter_dim1*iter_dim2*iter_dim3;

    // Main loop
    for (int i=0;i<n_iter;++i) {

      // Data pointers
      const T* a = a_perm;
      const T* b = b_perm;
      T* c = c_perm;

      // Construct indices
      int sub = i;
      for (int j=0;j<n-3;++j) {
        int ind = sub % ptr_iter_dims[j];
        a+= ptr_strides_a[j]*ind;
        b+= ptr_strides_b[j]*ind;
        c+= ptr_strides_c[j]*ind;
        sub/= ptr_iter_dims[j];
      }

      const T* a1 = a;
      const T* b1 = b;
      T* c1 = c;
      for (int i1=0;i1<iter_dim1;++i1) {
        const T* a2 = a1;
        const T* b2 = b1;
        T* c2 = c1;
        for (int i2=0;i2<iter_dim2;++i2) {
          const T* a3 = a2;
          const T* b3 = b2;
          T* c3 = c2;
          for (int i3=0;i3<iter_dim3;++i3) {
            // Perform the actual multiplication
            Contraction<T>(*a3, *b3, *c3);

            a3+= stridea3;
            b3+= strideb3;
            c3+= stridec3;
          }
          a2+= stridea2;
          b2+= strideb2;
          c2+= stridec2;
        }
        a1+= stridea1;
        b1+= strideb1;
        c1+= stridec1;
      }


    }

  }

}// namespace casadi

#endif // CASADI_CASADI_SHARED_HPP
