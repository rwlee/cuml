/*
 * Copyright (c) 2019, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cuda_utils.h>
#include <stdint.h>
#include <limits>

namespace MLCommon {
namespace Distance {

DI void sts(float* addr, const float& x) { *addr = x; }
DI void sts(float* addr, const float (&x)[1]) { *addr = x[0]; }
DI void sts(float* addr, const float (&x)[2]) {
  float2 v2 = make_float2(x[0], x[1]);
  auto* s2 = reinterpret_cast<float2*>(addr);
  *s2 = v2;
}
DI void sts(float* addr, const float (&x)[4]) {
  float4 v4 = make_float4(x[0], x[1], x[2], x[3]);
  auto* s4 = reinterpret_cast<float4*>(addr);
  *s4 = v4;
}
DI void sts(double* addr, const double& x) { *addr = x; }
DI void sts(double* addr, const double (&x)[1]) { *addr = x[0]; }
DI void sts(double* addr, const double (&x)[2]) {
  double2 v2 = make_double2(x[0], x[1]);
  auto* s2 = reinterpret_cast<double2*>(addr);
  *s2 = v2;
}

DI void lds(float& x, float* addr) { x = *addr; }
DI void lds(float (&x)[1], float* addr) { x[0] = *addr; }
DI void lds(float (&x)[2], float* addr) {
  auto* s2 = reinterpret_cast<float2*>(addr);
  auto v2 = *s2;
  x[0] = v2.x;
  x[1] = v2.y;
}
DI void lds(float (&x)[4], float* addr) {
  auto* s4 = reinterpret_cast<float4*>(addr);
  auto v4 = *s4;
  x[0] = v4.x;
  x[1] = v4.y;
  x[2] = v4.z;
  x[3] = v4.w;
}
DI void lds(double& x, double* addr) { x = *addr; }
DI void lds(double (&x)[1], double* addr) { x[0] = *addr; }
DI void lds(double (&x)[2], double* addr) {
  auto* s2 = reinterpret_cast<double2*>(addr);
  auto v2 = *s2;
  x[0] = v2.x;
  x[1] = v2.y;
}

DI void ldg(float& x, float* addr) {
  asm volatile("ld.global.cg.f32 %0, [%1];" : "=f"(x) : "l"(addr));
}
DI void ldg(float (&x)[1], float* addr) {
  asm volatile("ld.global.cg.f32 %0, [%1];" : "=f"(x[0]) : "l"(addr));
}
DI void ldg(float (&x)[2], float* addr) {
  asm volatile("ld.global.cg.v2.f32 {%0, %1}, [%2];"
               : "=f"(x[0]), "=f"(x[1])
               : "l"(addr));
}
DI void ldg(float (&x)[4], float* addr) {
  asm volatile("ld.global.cg.v4.f32 {%0, %1, %2, %3}, [%4];"
               : "=f"(x[0]), "=f"(x[1]), "=f"(x[2]), "=f"(x[3])
               : "l"(addr));
}
DI void ldg(double& x, double* addr) {
  asm volatile("ld.global.cg.f64 %0, [%1];" : "=d"(x) : "l"(addr));
}
DI void ldg(double (&x)[1], double* addr) {
  asm volatile("ld.global.cg.f64 %0, [%1];" : "=d"(x[0]) : "l"(addr));
}
DI void ldg(double (&x)[2], double* addr) {
  asm volatile("ld.global.cg.v2.f64 {%0, %1}, [%2];"
               : "=d"(x[0]), "=d"(x[1])
               : "l"(addr));
}

template <typename K, typename V>
struct KVP {
  K k;
  V v;
};

template <typename DataT, int _veclen, int _kblk, int _rpt, int _cpt, int _tr,
          int _tc>
struct KernelPolicy {
  enum {
    /** number of elements along K worked upon per main loop iteration */
    Kblk = _kblk,
    /** number of elements loaded per LDG */
    Veclen = _veclen,
    /** number of rows a thread works on for accumulation */
    AccRowsPerTh = _rpt,
    /** number of cols a thread works on for accumulation */
    AccColsPerTh = _cpt,
    /** number of threads working the same output col */
    AccThRows = _tr,
    /** number of threads working the same output row */
    AccThCols = _tc,
    /** total threads per block */
    Nthreads = AccThRows * AccThCols,
    /** output tile size along rows */
    Mblk = AccRowsPerTh * AccThRows,
    /** output tile size along cols */
    Nblk = AccColsPerTh * AccThCols,
    /** number of threads loading a single row */
    LdgThK = Kblk / Veclen,
    /** number of LDGs issued by a single thread for X */
    LdgPerThX = Mblk * LdgThK / Nthreads,
    /** number of LDGs issued by a single thread for Y */
    LdgPerThY = Nblk * LdgThK / Nthreads,
    /** number of rows of X covered per LDG */
    LdgRowsX = Mblk / LdgPerThX,
    /** number of rows of Y covered per LDG */
    LdgRowsY = Nblk / LdgPerThY,
    /** stride for accessing X/Y data in shared mem */
    SmemStride = Kblk + Veclen,
    /** size of one page for storing X data */
    SmemPageX = SmemStride * Mblk,
    /** size of one page for storing Y data */
    SmemPageY = SmemStride * Nblk,
    /** size of one smem page */
    SmemPage = SmemPageX + SmemPageY,
    /** size (in B) for smem needed */
    ///@todo: enable double-buffering
    SmemSize = SmemPage * sizeof(DataT),
  };  // enum
};    // struct KernelPolicy

template <typename DataT, typename OutT, typename IdxT, typename Policy>
struct FusedL2NN {
 private:
  typedef Policy P;

  IdxT m, n, k, xrowid, yrowid;
  DataT *x, *y, *xn, *yn, *minDist;
  OutT* min;
  int* mutex;

  int srowid, scolid;
  int accrowid, acccolid;

  DataT *sx, *sy;
  DataT *sxNorm, *syNorm;
  int pageWr;

  DataT maxVal;

  DataT acc[P::AccRowsPerTh][P::AccColsPerTh];
  DataT regx[P::AccRowsPerTh][P::Veclen], regy[P::AccColsPerTh][P::Veclen];

  static const DataT Zero = (DataT)0;
  static const DataT Two = (DataT)2.0;

 public:
  DI FusedL2NN(OutT* _min, DataT* _minDist, DataT* _x, DataT* _y, DataT* _xn,
               DataT* _yn, IdxT _m, IdxT _n, IdxT _k, char* _smem, DataT _mv,
               int* _mut)
    : m(_m),
      n(_n),
      k(_k),
      xrowid(IdxT(blockIdx.x) * P::Mblk + threadIdx.x / P::LdgThK),
      yrowid(IdxT(blockIdx.y) * P::Nblk + threadIdx.x / P::LdgThK),
      x(_x + xrowid * k),
      y(_y + yrowid * k),
      xn(_xn),
      yn(_yn),
      minDist(_minDist),
      min(_min),
      mutex(_mut),
      srowid(threadIdx.x / P::LdgThK),
      scolid((threadIdx.x % P::LdgThK) * P::Veclen),
      accrowid(threadIdx.x / P::AccThCols),
      acccolid(threadIdx.x % P::AccThCols),
      sx((DataT*)_smem),
      sy(&(sx[P::SmemPageX])),
      sxNorm((DataT*)_smem),
      syNorm(&(sxNorm[P::Mblk])),
      pageWr(0),
      maxVal(_mv) {}

  DI void run() {
    prolog();
    loop();
    epilog();
  }

 private:
  DI void prolog() {
    //ldgsts(0);
#pragma unroll
    for (int i = 0; i < P::AccRowsPerTh; ++i) {
#pragma unroll
      for (int j = 0; j < P::AccColsPerTh; ++j) {
        acc[i][j] = Zero;
      }
    }
    //__syncthreads();
  }

  DI void ldgsts(IdxT kidx) {
    ldgstsX(kidx, sx + pageWr * P::SmemPage);
    ldgstsY(kidx, sy + pageWr * P::SmemPage);
    //pageWr ^= 1;
  }

  DI void ldgstsX(IdxT kidx, DataT* smem) {
    DataT data[P::LdgPerThX][P::Veclen];
    // LDG
    auto koffset = kidx + scolid;
    for (int i = 0; i < P::LdgPerThX; ++i) {
      if (koffset < k && (xrowid + i * P::LdgRowsX) < m) {
        ldg(data[i], x + i * P::LdgRowsX * k + koffset);
      } else {
#pragma unroll
        for (int j = 0; j < P::Veclen; ++j) {
          data[i][j] = Zero;
        }
      }
    }
    // STS
    auto* saddr = smem + srowid * P::SmemStride + scolid;
#pragma unroll
    for (int i = 0; i < P::LdgPerThX; ++i) {
      sts(saddr + i * P::LdgRowsX * P::SmemStride, data[i]);
    }
  }

  DI void ldgstsY(IdxT kidx, DataT* smem) {
    DataT data[P::LdgPerThX][P::Veclen];
    // LDG
    auto koffset = kidx + scolid;
    for (int i = 0; i < P::LdgPerThY; ++i) {
      if (koffset < k && (yrowid + i * P::LdgRowsY) < n) {
        ldg(data[i], y + i * P::LdgRowsY * k + koffset);
      } else {
#pragma unroll
        for (int j = 0; j < P::Veclen; ++j) {
          data[i][j] = Zero;
        }
      }
    }
    // STS
    auto* saddr = smem + srowid * P::SmemStride + scolid;
#pragma unroll
    for (int i = 0; i < P::LdgPerThY; ++i) {
      sts(saddr + i * P::LdgRowsY * P::SmemStride, data[i]);
    }
  }

  DI void ldsXY(int kidx) {
    ldsX(kidx, sx + pageWr * P::SmemPage);
    ldsY(kidx, sy + pageWr * P::SmemPage);
  }

  DI void ldsX(int kidx, DataT* smem) {
    auto* saddr = smem + accrowid * P::SmemStride + kidx;
#pragma unroll
    for (int i = 0; i < P::AccRowsPerTh; ++i) {
      lds(regx[i], saddr + i * P::AccThRows * P::SmemStride);
    }
  }

  DI void ldsY(int kidx, DataT* smem) {
    auto* saddr = smem + acccolid * P::SmemStride + kidx;
#pragma unroll
    for (int i = 0; i < P::AccColsPerTh; ++i) {
      lds(regy[i], saddr + i * P::AccThCols * P::SmemStride);
    }
  }

  DI void accumulate() {
#pragma unroll
    for (int i = 0; i < P::AccRowsPerTh; ++i) {
#pragma unroll
      for (int j = 0; j < P::AccColsPerTh; ++j) {
#pragma unroll
        for (int v = 0; v < P::Veclen; ++v) {
          acc[i][j] += regx[i][v] * regy[j][v];
        }
      }
    }
  }

  DI void loop() {
    for (int kidx = 0; kidx < k; kidx += P::Kblk) {
      ldgsts(kidx);
      __syncthreads();
#pragma unroll
      for (int ki = 0; ki < P::Kblk; ki += P::Veclen) {
        ldsXY(ki);
        accumulate();
        if (ki == P::Kblk - P::Veclen) {
          __syncthreads();
        }
      }
    }
  }

  DI void epilog() {
    __syncthreads();  // so that we can safely reuse smem
    for (int i = threadIdx.x; i < P::Mblk; i += P::Nthreads) {
      auto idx = blockIdx.x * P::Mblk + i;
      sxNorm[i] = idx < m ? xn[idx] : maxVal;
    }
    for (int i = threadIdx.x; i < P::Nblk; i += P::Nthreads) {
      auto idx = blockIdx.y * P::Nblk + i;
      syNorm[i] = idx < n ? yn[idx] : maxVal;
    }
    __syncthreads();
    DataT regxn[P::AccRowsPerTh], regyn[P::AccColsPerTh];
#pragma unroll
    for (int i = 0; i < P::AccRowsPerTh; ++i) {
      regxn[i] = sxNorm[i * P::AccThRows + accrowid];
    }
#pragma unroll
    for (int i = 0; i < P::AccColsPerTh; ++i) {
      regyn[i] = syNorm[i * P::AccThCols + acccolid];
    }
// compute
#pragma unroll
    for (int i = 0; i < P::AccRowsPerTh; ++i) {
#pragma unroll
      for (int j = 0; j < P::AccColsPerTh; ++j) {
        acc[i][j] = regxn[i] + regyn[j] - Two * acc[i][j];
      }
    }
    // reduce
    KVP<OutT, DataT> val[P::AccRowsPerTh];
    auto lid = threadIdx.x % WarpSize;
#pragma unroll
    for (int i = 0; i < P::AccRowsPerTh; ++i) {
      val[i].k = -1;
      val[i].v = maxVal;
#pragma unroll
      for (int j = 0; j < P::AccColsPerTh; ++j) {
        auto tmpk = acccolid + j * P::AccThCols + blockIdx.y * P::Nblk;
        if (tmpk < n && acc[i][j] < val[i].v) {
          val[i].k = tmpk;
          val[i].v = acc[i][j];
        }
      }
#pragma unroll
      for (int j = P::AccThCols / 2; j > 0; j >>= 1) {
        auto tmpk = shfl(val[i].k, lid + j);
        auto tmpv = shfl(val[i].v, lid + j);
        if (tmpv < val[i].v) {
          val[i].k = tmpk;
          val[i].v = tmpv;
        }
      }
    }
    if (lid % P::AccThCols == 0) {
      auto ridx = IdxT(blockIdx.x) * P::Mblk + accrowid;
#pragma unroll
      for (int i = 0; i < P::AccRowsPerTh; ++i) {
        auto rid = ridx + i * P::AccThRows;
        if (rid < m) {
          while (atomicCAS(mutex + rid, 0, 1) == 1)
            ;
          auto tmpv = minDist[rid];
          if (val[i].v < tmpv) {
            min[rid] = val[i].k;
            minDist[rid] = val[i].v;
          }
          __threadfence();
          atomicCAS(mutex + rid, 1, 0);
        }
      }
    }
  }
};

template <typename DataT, typename OutT, typename IdxT, typename Policy>
__global__ __launch_bounds__(Policy::Nthreads, 2) void fusedL2NNkernel(
  OutT* min, DataT* minDist, DataT* x, DataT* y, DataT* xn, DataT* yn, IdxT m,
  IdxT n, IdxT k, DataT maxVal, int* mutex) {
  extern __shared__ char smem[];
  FusedL2NN<DataT, OutT, IdxT, Policy> obj(min, minDist, x, y, xn, yn, m, n, k,
                                           smem, maxVal, mutex);
  obj.run();
}

template <typename DataT, typename OutT, typename IdxT>
__global__ void initKernel(OutT* min, DataT* minDist, IdxT m, DataT maxVal) {
  auto tid = IdxT(blockIdx.x) * blockDim.x + threadIdx.x;
  if (tid < m) {
    min[tid] = -1;
    minDist[tid] = maxVal;
  }
}

template <typename DataT, typename OutT, typename IdxT, int VecLen>
void fusedL2NNImpl(OutT* min, DataT* minDist, DataT* x, DataT* y, DataT* xn,
                   DataT* yn, IdxT m, IdxT n, IdxT k, int* workspace,
                   cudaStream_t stream) {
  typedef KernelPolicy<DataT, VecLen, 32, 4, 4, 16, 16> Policy;
  dim3 grid(ceildiv<int>(m, Policy::Mblk), ceildiv<int>(n, Policy::Nblk));
  dim3 blk(Policy::Nthreads);
  auto nblks = ceildiv<int>(m, Policy::Nthreads);
  auto maxVal = std::numeric_limits<DataT>::max();
  CUDA_CHECK(cudaMemsetAsync(workspace, 0, sizeof(int) * m, stream));
  initKernel<DataT, IdxT>
    <<<nblks, Policy::Nthreads, 0, stream>>>(min, minDist, m, maxVal);
  CUDA_CHECK(cudaGetLastError());
  fusedL2NNkernel<DataT, OutT, IdxT, Policy>
    <<<grid, blk, Policy::SmemSize, stream>>>(min, minDist, x, y, xn, yn, m, n,
                                              k, maxVal, workspace);
  CUDA_CHECK(cudaGetLastError());
}

/**
 * @brief Fused L2 distance and 1-nearest-neighbor computation in a single call.
 *        The benefits of such a call are 2-fold: 1) eliminate the need for an
 *        intermediate buffer to store the output of gemm 2) reduce the memory
 *        read traffic on this intermediate buffer, otherwise needed during the
 *        reduction phase for 1-NN.
 * @tparam DataT data type
 * @tparam OutT output type to store 1-NN indices
 * @tparam IdxT indexing arithmetic type
 * @param[out] min will contain the indicies for 1-NN computation. Length = `m`.
 *                 It should be on device.
 * @param[out] minDist will contain the minimum distance value from the 1-NN
 *                     computation. Length = `m`. It should be on device.
 * @param[in] x first matrix. Row major. Dim = `m x k`. Should be on device.
 * @param[in] y second matrix. Row major. Dim = `n x k`. Should be on device.
 * @param[in] xn L2 squared norm of `x`. Length = `m`. It should be on device.
 * @param[in] yn L2 squared norm of `y`. Length = `n`. It should be on device.
 * @param[in] m gemm m
 * @param[in] n gemm n
 * @param[in] k gemm k
 * @param[in] workspace temporary workspace. Length = `m`. Should be on device.
 * @param[in] stream cuda stream
 */
template <typename DataT, typename OutT, typename IdxT>
void fusedL2NN(OutT* min, DataT* minDist, DataT* x, DataT* y, DataT* xn,
               DataT* yn, IdxT m, IdxT n, IdxT k, int* workspace,
               cudaStream_t stream) {
  if (k % 4 == 0) {
    fusedL2NNImpl<DataT, OutT, IdxT, 4>(min, minDist, x, y, xn, yn, m, n, k,
                                        workspace, stream);
  } else if (k % 2 == 0) {
    fusedL2NNImpl<DataT, OutT, IdxT, 2>(min, minDist, x, y, xn, yn, m, n, k,
                                        workspace, stream);
  } else {
    fusedL2NNImpl<DataT, OutT, IdxT, 1>(min, minDist, x, y, xn, yn, m, n, k,
                                        workspace, stream);
  }
}

}  // namespace Distance
}  // namespace MLCommon
