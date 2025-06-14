#ifndef __GEMM_H__
#define __GEMM_H__ 1
#include <x86intrin.h>
#include <pthread.h>
#include <string.h>

#define US_PER_S 1000000
#define GIGA     1000000000

#define BLOCK_I 64 // in bytes -> 128x128 block
                    // a 64x64 block of floats uses 16K of memory (64KB L1d cache on this CPU - i5-8350u)
#define BLOCK_J 64 
#define BLOCK_K 1024 
#define SBLOCK_I 2048 
#define SBLOCK_J 2048 
#define SBLOCK_K 2048 

template<typename T>
void simd_gemm(T *mat1, T *mat2, T *dst, int n);
void* simd_gemm_worker(void *argv);
void* simd_gemm_worker2(void *argv);

inline void gemm_inner(float *mat1_ptr, float *mat2_ptr, float *dst_ptr, int simd_ele_width, int block_ele_width);
inline void gemm_inner(double *mat1_ptr, double *mat2_ptr, double *dst_ptr, int simd_ele_width, int block_ele_width);
void cpu_transpose(float *mat, int n);

typedef struct{
    float *mat1;
    float *mat2;
    float *dst;
    int    n;
    int    th_id;
} gemmOptns;

void simd_gemm(float * __restrict mat1, float * __restrict mat2, float * __restrict dst, int n)
{
    gemmOptns thd_optns[NUM_THREADS];
    pthread_t thds[NUM_THREADS];

    for (int th_id = 0; th_id < NUM_THREADS; ++th_id)
    {
        gemmOptns optn =  {mat1, mat2, dst, n, th_id};
        thd_optns[th_id] = optn;
        pthread_create(&thds[th_id], NULL, &simd_gemm_worker, (void*)&thd_optns[th_id]);

    }

    for (int th_id = 0; th_id < NUM_THREADS; ++th_id)
    {
        pthread_join(thds[th_id], NULL);
    }

}

void* simd_gemm_worker(void *argv)
{
    gemmOptns *optns = (gemmOptns*)argv;
    float *mat1 = optns->mat1;
    float *mat2 = optns->mat2;
    float *dst  = optns->dst;
    int    n    = optns->n;
    int    th_id = optns->th_id;
    int    thd_loop_sz = n / NUM_THREADS;
    int    start_idx = thd_loop_sz * th_id;
    int    stop_idx  = start_idx + thd_loop_sz;

    int simd_ele_width  = SIMD_WIDTH  / sizeof(float);
    int block_ele_i = BLOCK_I / sizeof(float);
    int block_ele_j = BLOCK_J / sizeof(float);
    int block_ele_k = BLOCK_K / sizeof(float);
    int sblock_ele_i = SBLOCK_I / sizeof(float);
    int sblock_ele_j = SBLOCK_J / sizeof(float);
    int sblock_ele_k = SBLOCK_K / sizeof(float);
    //int vec_n = n / simd_ele_width;
    int block_ni = sblock_ele_i/block_ele_i;
    int block_nj = sblock_ele_j/block_ele_j;
    int block_nk = sblock_ele_k/block_ele_k;

    float * __restrict mat1_ptr, * __restrict mat2_ptr, * __restrict dst_ptr;
    float * __restrict mat2_ptr2, * __restrict mat2_ptr3, * __restrict mat2_ptr4;

    for (int i_outer = start_idx; i_outer < stop_idx; i_outer += sblock_ele_i)
    {
        for (int j_outer = 0; j_outer < n; j_outer += sblock_ele_j)
        {
            for (int k_outer = 0; k_outer < n; k_outer += sblock_ele_k)
            {
                float packed_b[sblock_ele_j*sblock_ele_k] __attribute__ ((__aligned__(64)));
                float packed_a[sblock_ele_k*sblock_ele_i] __attribute__ ((__aligned__(64)));

                for (int idx = 0; idx < sblock_ele_i; ++idx)
                {
                    mat1_ptr = mat1 + (i_outer + idx)*n + k_outer;
                    for (int jdx = 0; jdx < sblock_ele_k; jdx += block_ele_k)
                    {
                    //memcpy(packed_b + jdx*block_ele_width + (idx%block_ele_width) * block_ele_width + (idx/block_ele_width)*block_ele_width*block_ele_width*block_n, mat2 + (j_outer + idx)*n + jdx + k_outer, BLOCK_WIDTH);
                    memcpy(packed_a + jdx*block_ele_i + (idx%block_ele_i) * block_ele_k + (idx/block_ele_i)*block_ele_i*block_ele_k*block_nk, mat1_ptr + jdx, BLOCK_K);
                    }
                }
                for (int idx = 0; idx < sblock_ele_j; ++idx)
                {
                    mat2_ptr = mat2 + (j_outer + idx)*n + k_outer;
                    for (int jdx = 0; jdx < sblock_ele_k; jdx += block_ele_k)
                    {
                    memcpy(packed_b + jdx*block_ele_j+ (idx%block_ele_j) * block_ele_k + (idx/block_ele_j)*block_ele_k*block_ele_j*block_nk, mat2_ptr + jdx, BLOCK_K);
                    //memcpy(packed_a + jdx*block_ele_width + (idx%block_ele_width) * block_ele_width + (idx/block_ele_width)*block_ele_width*block_ele_width*block_n, mat1 + (i_outer + idx)*n + jdx + k_outer, BLOCK_WIDTH);
                    }
                }

    for (int i_outer2 = 0; i_outer2< sblock_ele_i; i_outer2+= block_ele_i)
    {
        for (int j_outer2= 0; j_outer2< sblock_ele_j; j_outer2 += block_ele_j)
        {
            for (int k_outer2= 0; k_outer2< sblock_ele_k; k_outer2 += block_ele_k)
            {
                for (int i_inner = 0; i_inner < block_ele_i; ++i_inner)
                {
                    mat1_ptr = packed_a + (i_outer2*block_nk + i_inner)*block_ele_k + k_outer2*block_ele_i;
                    //_mm_prefetch(mat1_ptr, _MM_HINT_T0);

                    //dst_ptr = dst_tmp + i_inner*block_ele_width; // + (i_outer + i_inner)*n + j_outer;
                    dst_ptr = dst + (i_outer + i_outer2 + i_inner)*n + j_outer + j_outer2;
                    //_mm_prefetch(dst_ptr, _MM_HINT_T0); 
                    
                    for (int j_inner = 0; j_inner < block_ele_j; j_inner += simd_ele_width)
                    {

                            __m256 a_vec, b_vec, b_vec2, b_vec3, b_vec4;
                            __m256 dst2 = _mm256_setzero_ps();

                            __m256 sums[simd_ele_width];
                            for (int idx = 0; idx < simd_ele_width; ++idx)
                                sums[idx] = _mm256_setzero_ps();

                            mat2_ptr  = packed_b + (j_inner + j_outer2*block_nk + 0)*block_ele_k + k_outer2*block_ele_j;
                            mat2_ptr2 = packed_b + (j_inner + j_outer2*block_nk+ 1)*block_ele_k + k_outer2*block_ele_j;
                            mat2_ptr3 = packed_b + (j_inner + j_outer2*block_nk+ 2)*block_ele_k + k_outer2*block_ele_j;
                            mat2_ptr4 = packed_b + (j_inner + j_outer2*block_nk+ 3)*block_ele_k + k_outer2*block_ele_j;
                            for (int k_inner = 0; k_inner < block_ele_k; k_inner += simd_ele_width)
                            {
                                a_vec = _mm256_load_ps( mat1_ptr + k_inner );
                                b_vec = _mm256_load_ps( mat2_ptr + k_inner );
                                sums[0] = _mm256_fmadd_ps(a_vec, b_vec, sums[0]);
                                b_vec2 = _mm256_load_ps( mat2_ptr2 + k_inner );
                                sums[1] = _mm256_fmadd_ps(a_vec, b_vec2, sums[1]);
                                b_vec3 = _mm256_load_ps( mat2_ptr3 + k_inner );
                                sums[2] = _mm256_fmadd_ps(a_vec, b_vec3, sums[2]);
                                b_vec4 = _mm256_load_ps( mat2_ptr4 + k_inner );
                                sums[3] = _mm256_fmadd_ps(a_vec, b_vec4, sums[3]);
                            }

                            __m256 lower, upper, hsum, shuf;
                            mat2_ptr  = packed_b + (j_inner + j_outer2*block_nk + 4)*block_ele_k + k_outer2*block_ele_j;
                            mat2_ptr2 = packed_b + (j_inner + j_outer2*block_nk + 5)*block_ele_k + k_outer2*block_ele_j;
                            mat2_ptr3 = packed_b + (j_inner + j_outer2*block_nk + 6)*block_ele_k + k_outer2*block_ele_j;
                            mat2_ptr4 = packed_b + (j_inner + j_outer2*block_nk + 7)*block_ele_k+ k_outer2*block_ele_j;
                                a_vec = _mm256_load_ps( mat1_ptr + 0 );
                                b_vec = _mm256_load_ps( mat2_ptr + 0 );
                                sums[4] = _mm256_fmadd_ps(a_vec, b_vec, sums[4]);
                                b_vec2 = _mm256_load_ps( mat2_ptr2 + 0 );
                                sums[5] = _mm256_fmadd_ps(a_vec, b_vec2, sums[5]);
                                b_vec3 = _mm256_load_ps( mat2_ptr3 + 0 );
                                sums[6] = _mm256_fmadd_ps(a_vec, b_vec3, sums[6]);
                                b_vec4 = _mm256_load_ps( mat2_ptr4 + 0 );
                                sums[7] = _mm256_fmadd_ps(a_vec, b_vec4, sums[7]);
                            lower = _mm256_permute2f128_ps(sums[0], sums[1], 0x20);
                            upper = _mm256_permute2f128_ps(sums[0], sums[1], 0x31);
                            hsum  = _mm256_add_ps(lower, upper);
                            shuf  = _mm256_permute_ps(hsum, 0x1B);
                                   hsum  = _mm256_add_ps(hsum, shuf);
                                   shuf  = _mm256_permute_ps(hsum, 0xB1);
                                   hsum  = _mm256_add_ps(hsum, shuf);
                            dst2 = _mm256_blend_ps(dst2, hsum, 0x21);
                            
                                a_vec = _mm256_load_ps( mat1_ptr + simd_ele_width );
                                b_vec = _mm256_load_ps( mat2_ptr + simd_ele_width );
                                sums[4] = _mm256_fmadd_ps(a_vec, b_vec, sums[4]);
                                b_vec2 = _mm256_load_ps( mat2_ptr2 + simd_ele_width );
                                sums[5] = _mm256_fmadd_ps(a_vec, b_vec2, sums[5]);
                                b_vec3 = _mm256_load_ps( mat2_ptr3 + simd_ele_width );
                                sums[6] = _mm256_fmadd_ps(a_vec, b_vec3, sums[6]);
                                b_vec4 = _mm256_load_ps( mat2_ptr4 + simd_ele_width );
                                sums[7] = _mm256_fmadd_ps(a_vec, b_vec4, sums[7]);
                            lower = _mm256_permute2f128_ps(sums[2], sums[3], 0x20);
                            upper = _mm256_permute2f128_ps(sums[2], sums[3], 0x31);
                            hsum  = _mm256_add_ps(lower, upper);
                            shuf  = _mm256_permute_ps(hsum, 0x1B);
                            hsum  = _mm256_add_ps(hsum, shuf);
                            shuf  = _mm256_permute_ps(hsum, 0xB1);
                            hsum  = _mm256_add_ps(hsum, shuf);
                            dst2  = _mm256_blend_ps(dst2, hsum, 0x84);
                            for (int k_inner = simd_ele_width*2; k_inner < block_ele_k; k_inner += simd_ele_width)
                            {
                                a_vec = _mm256_load_ps( mat1_ptr + k_inner );
                                b_vec = _mm256_load_ps( mat2_ptr + k_inner );
                                sums[4] = _mm256_fmadd_ps(a_vec, b_vec, sums[4]);
                                b_vec2 = _mm256_load_ps( mat2_ptr2 + k_inner );
                                sums[5] = _mm256_fmadd_ps(a_vec, b_vec2, sums[5]);
                                b_vec3 = _mm256_load_ps( mat2_ptr3 + k_inner );
                                sums[6] = _mm256_fmadd_ps(a_vec, b_vec3, sums[6]);
                                b_vec4 = _mm256_load_ps( mat2_ptr4 + k_inner );
                                sums[7] = _mm256_fmadd_ps(a_vec, b_vec4, sums[7]);
                            }


                            lower = _mm256_permute2f128_ps(sums[4], sums[5], 0x20);
                            upper = _mm256_permute2f128_ps(sums[4], sums[5], 0x31);
                            hsum  = _mm256_add_ps(lower, upper);
                            lower = _mm256_permute2f128_ps(sums[6], sums[7], 0x20);
                            upper = _mm256_permute2f128_ps(sums[6], sums[7], 0x31);
                            shuf  = _mm256_permute_ps(hsum, 0x1B);
                            hsum  = _mm256_add_ps(hsum, shuf);
                            shuf  = _mm256_permute_ps(hsum, 0xB1);
                            hsum  = _mm256_add_ps(hsum, shuf);
                            dst2  = _mm256_blend_ps(dst2, hsum, 0x12);

                            hsum  = _mm256_add_ps(lower, upper);
                            shuf  = _mm256_permute_ps(hsum, 0x1B);
                            hsum  = _mm256_add_ps(hsum, shuf);
                            shuf  = _mm256_permute_ps(hsum, 0xB1);
                            hsum  = _mm256_add_ps(hsum, shuf);
                            dst2  = _mm256_blend_ps(dst2, hsum, 0x48);

                            __m256 swapd = _mm256_permute2f128_ps(dst2, dst2, 0x21);
                            dst2 = _mm256_blend_ps(dst2, swapd, 0xAA);
                            swapd = _mm256_permute_ps(dst2, 0xB1);
                            dst2 = _mm256_blend_ps(dst2, swapd, 0xF0);

                        //__m256 sums = _mm256_load_ps(temp); 
                        //__m256 sums = _mm256_setzero_ps(); 
                        __m256 dst_vec = _mm256_load_ps(dst_ptr);
                        dst2 = _mm256_add_ps(dst2, dst_vec);
                        _mm256_store_ps(dst_ptr, dst2);
                        dst_ptr += simd_ele_width;
                       }}} 
                    }
                }
            }
        }
    }
    return NULL;
}

#endif  // __GEMM_H__
