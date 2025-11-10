//
// Copyright (c) 2015, Hannes Roest
// All rights reserved.
//
// This software is released under a three-clause BSD license:
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef CPPLOWESS_LOWESS_H
#define CPPLOWESS_LOWESS_H

#include <stdlib.h>
#include <cmath>
#include <algorithm>    // std::min, std::max

namespace CppLowess
{

  /// Templated lowess class
  template <typename ContainerType, typename ValueType>
  class TemplatedLowess
  {
    inline ValueType pow2(ValueType x) { return x * x;  }
    inline ValueType pow3(ValueType x) { return x * x * x;  }

    /// Calculate smoothed/fitted y-value using weighted linear regression.
    bool lowest(const ContainerType& x,
                const ContainerType& y,
                size_t n,
                const ValueType& xs,
                ValueType& ys,
                size_t nleft,
                size_t nright,
                ContainerType& w,
                bool userw,
                const ContainerType& rw)
    {
      ValueType range = x[n - 1] - x[0];
      ValueType h = std::max(xs - x[nleft], x[nright] - xs);
      ValueType h9 = .999 * h;
      ValueType h1 = .001 * h;
      ValueType a = 0.0;

      // Compute weights
      for (size_t j = nleft; j < n; j++)
      {
        w[j] = 0.0;
        ValueType r = std::abs(x[j] - xs);
        if (r <= h9)
        {
          if (r > h1)
          {
            w[j] = pow3(1.0 - pow3(r / h));
          }
          else
          {
            w[j] = 1.0;
          }
          if (userw) w[j] = rw[j] * w[j];
          a = a + w[j];
        }
        else if (x[j] > xs) break;
      }

      size_t nrt = nleft;
      for (size_t i = nleft; i < n; i++)
      {
        if (w[i] > 0) nrt = i;
        else if (x[i] > xs) break;
      }

      if (a <= 0.0)
      {
        return false;
      }
      else
      {
        // Normalize weights
        for (size_t j = nleft; j <= nrt; j++)
        {
          w[j] = w[j] / a;
        }

        if (h > 0.0)
        {
          // Weighted least squares
          a = 0.0;
          for (size_t j = nleft; j <= nrt; j++)
          {
            a = a + w[j] * x[j];
          }

          ValueType b = xs - a;
          ValueType c = 0.0;
          for (size_t j = nleft; j <= nrt; j++)
          {
            c = c + w[j] * pow2(x[j] - a);
          }

          if (std::sqrt(c) > .001 * range)
          {
            b = b / c;
            for (size_t j = nleft; j <= nrt; j++)
            {
              w[j] = w[j] * (1.0 + b * (x[j] - a));
            }
          }
        }

        ys = 0.0;
        for (size_t j = nleft; j <= nrt; j++)
        {
          ys = ys + w[j] * y[j];
        }
      }
      return true;
    }

    /// Update the neighborhood for the current point
    void update_neighborhood(const ContainerType& x,
                            const size_t n,
                            const size_t i,
                            size_t& nleft,
                            size_t& nright)
    {
      ValueType d1, d2;

      while (nright < n - 1)
      {
        d1 = x[i] - x[nleft];
        d2 = x[nright + 1] - x[i];
        if (d1 <= d2) break;
        nleft++;
        nright++;
      }
    }

    /// Update the counters of the local regression
    void update_indices(const ContainerType& x,
                        const size_t n,
                        const ValueType delta,
                        size_t& i,
                        size_t& last,
                        ContainerType& ys)
    {
      last = i;
      ValueType cut = x[last] + delta;
      for (i = last + 1; i < n; i++)
      {
        if (x[i] > cut) break;
        if (x[i] == x[last])
        {
          ys[i] = ys[last];
          last = i;
        }
      }
      i = std::max(last + 1, i - 1);
    }

    /// Calculate smoothed/fitted y by linear interpolation
    void interpolate_skipped_fits(const ContainerType& x,
                                  const size_t i,
                                  const size_t last,
                                  ContainerType& ys)
    {
      ValueType alpha;
      ValueType denom = x[i] - x[last];
      for (size_t j = last + 1; j < i; j = j + 1)
      {
        alpha = (x[j] - x[last]) / denom;
        ys[j] = alpha * ys[i] + (1.0 - alpha) * ys[last];
      }
    }

    /// Calculate residual weights for the next robustifying iteration
    void calculate_residual_weights(const size_t n,
                                    const ContainerType& weights,
                                    ContainerType& resid_weights)
    {
      ValueType r;

      for (size_t i = 0; i < n; i++)
      {
        resid_weights[i] = std::abs(weights[i]);
      }

      size_t m1 = n / 2;
      typename ContainerType::iterator it_m1 = resid_weights.begin() + m1;
      std::nth_element(resid_weights.begin(), it_m1, resid_weights.end());
      typename ContainerType::iterator it_m2 = std::max_element(
        resid_weights.begin(), it_m1);
      ValueType cmad = 3.0 * (*it_m1 + *it_m2);
      ValueType c9 = .999 * cmad;
      ValueType c1 = .001 * cmad;

      for (size_t i = 0; i < n; i++)
      {
        r = std::abs(weights[i]);
        if (r <= c1)
        {
          resid_weights[i] = 1.0;
        }
        else if (r > c9)
        {
          resid_weights[i] = 0.0;
        }
        else
        {
          resid_weights[i] = pow2(1.0 - pow2(r / cmad));
        }
      }
    }

public:
    int lowess(const ContainerType& x,
               const ContainerType& y,
               double frac,
               int nsteps,
               ValueType delta,
               ContainerType& ys,
               ContainerType& resid_weights,
               ContainerType& weights
               )
    {
      bool fit_ok;
      size_t i, last, nleft, nright, ns;

      size_t n = x.size();
      if (n < 2)
      {
        ys[0] = y[0];
        return 1;
      }

      size_t tmp = (size_t)(frac * (double)n);
      ns = std::max(std::min(tmp, n), (size_t)2);

      for (int iter = 1; iter <= nsteps + 1; iter++)
      {
        nleft = 0;
        nright = ns - 1;
        last = -1;
        i = 0;

        do
        {
          update_neighborhood(x, n, i, nleft, nright);
          fit_ok = lowest(x, y, n, x[i], ys[i], nleft, nright,
                          weights, (iter > 1), resid_weights);

          if (!fit_ok) ys[i] = y[i];

          if (last < i - 1)
          {
            interpolate_skipped_fits(x, i, last, ys);
          }

          update_indices(x, n, delta, i, last, ys);
        }
        while (last < n - 1);

        for (i = 0; i < n; i++)
        {
          weights[i] = y[i] - ys[i];
        }

        if (iter > nsteps) break;

        calculate_residual_weights(n, weights, resid_weights);
      }
      return 0;
    }
  };

  typedef TemplatedLowess<std::vector<double>, double> Lowess;

} // namespace CppLowess
#endif //CPPLOWESS_LOWESS_H

