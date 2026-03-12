/**
 * Wave521 32-bit Packed 420 10-bit Format Converter - NEON Optimized
 * 
 * Converts AMLOGIC YUV422 10-bit packed format to Wave521 format using ARM NEON:
 * - FORMAT_420_P10_32BIT_MSB: 32-bit packed with 2 zero MSB bits
 * - Layout per 32-bit word: |00|Sample0(10)|Sample1(10)|Sample2(10)|
 * 
 * Y plane: 3 Y samples packed per 32-bit word
 * UV plane: 3 UV pairs packed per 32-bit word (4:2:0 subsampled)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <arm_neon.h>
#include "yuv422_converter_neon.h"

/* NEON constants for 10-bit extraction */
#define TEN_BIT_MASK 0x3FF

/* Thread work structure */
typedef struct {
    int thread_id;
    int start_row;
    int end_row;
    int width;
    int height;
    const uint8_t *input;
    uint32_t *output_y;
    uint32_t *output_uv;
    int input_stride;
    int output_stride;
    CoreSelection core_selection;
} Wave521Work;

/**
 * NEON-optimized function to pack three 10-bit values into 32-bit word
 * Uses NEON to process multiple pixels simultaneously
 */
static inline void pack_10bit_neon(
    uint16x4_t val0,
    uint16x4_t val1, 
    uint16x4_t val2,
    uint32x4_t *result)
{
    /* Each vector contains 4 values */
    /* Pack: |00|val0(10)|val1(10)|val2(10)| */
    
    uint32x4_t v0 = vmovl_u16(val0);  /* Extend to 32-bit */
    uint32x4_t v1 = vmovl_u16(val1);
    uint32x4_t v2 = vmovl_u16(val2);
    
    /* Shift and combine */
    *result = v0;                                /* val0 at bits 0-9 */
    *result = vorrq_u32(*result, vshlq_n_u32(v1, 10));  /* val1 at bits 10-19 */
    *result = vorrq_u32(*result, vshlq_n_u32(v2, 20));  /* val2 at bits 20-29 */
}

/**
 * NEON-optimized row conversion
 * Processes 24 pixels (12 pairs) at a time using NEON
 */
static void convert_row_wave521_neon(
    const uint8_t *input_row0,
    const uint8_t *input_row1,
    uint32_t *output_y,
    uint32_t *output_uv,
    int width,
    int row_pair_idx)
{
    int x;
    int uv_idx = 0;
    
    /* Process 24 pixels (12 pairs) at a time with NEON */
    for (x = 0; x < width - 23; x += 24) {
        /* Load 60 bytes (24 pixels = 12 pairs * 5 bytes) */
        uint8x16_t data0 = vld1q_u8(input_row0 + x * 5 / 2);
        uint8x16_t data1 = vld1q_u8(input_row0 + x * 5 / 2 + 16);
        uint8x16_t data2 = vld1q_u8(input_row0 + x * 5 / 2 + 32);
        
        /* Similar load for row 1 (for chroma averaging) */
        uint8x16_t data0_r1 = vld1q_u8(input_row1 + x * 5 / 2);
        uint8x16_t data1_r1 = vld1q_u8(input_row1 + x * 5 / 2 + 16);
        
        /* Extract 10-bit values using NEON shifts and masks */
        /* This is complex due to the 40-bit packing - we'll use lookup tables */
        /* or shift operations to extract the values */
        
        /* For now, scalar fallback for the complex bit extraction */
        /* TODO: Optimize with NEON lookup table */
        int i;
        for (i = 0; i < 24; i += 6) {
            uint64_t pair0, pair1, pair2, pair3;
            uint16_t y_vals_r0[6], y_vals_r1[6];
            uint16_t u_vals_r0[3], v_vals_r0[3];
            uint16_t u_vals_r1[3], v_vals_r1[3];
            
            /* Decode 6 pixels from row 0 */
            pair0 = (uint64_t)input_row0[(x+i+0) * 5/2 + 0] |
                    ((uint64_t)input_row0[(x+i+0) * 5/2 + 1] << 8) |
                    ((uint64_t)input_row0[(x+i+0) * 5/2 + 2] << 16) |
                    ((uint64_t)input_row0[(x+i+0) * 5/2 + 3] << 24) |
                    ((uint64_t)input_row0[(x+i+0) * 5/2 + 4] << 32);
            
            pair1 = (uint64_t)input_row0[(x+i+2) * 5/2 + 0] |
                    ((uint64_t)input_row0[(x+i+2) * 5/2 + 1] << 8) |
                    ((uint64_t)input_row0[(x+i+2) * 5/2 + 2] << 16) |
                    ((uint64_t)input_row0[(x+i+2) * 5/2 + 3] << 24) |
                    ((uint64_t)input_row0[(x+i+2) * 5/2 + 4] << 32);
            
            pair2 = (uint64_t)input_row0[(x+i+4) * 5/2 + 0] |
                    ((uint64_t)input_row0[(x+i+4) * 5/2 + 1] << 8) |
                    ((uint64_t)input_row0[(x+i+4) * 5/2 + 2] << 16) |
                    ((uint64_t)input_row0[(x+i+4) * 5/2 + 3] << 24) |
                    ((uint64_t)input_row0[(x+i+4) * 5/2 + 4] << 32);
            
            /* Extract YUV for row 0 */
            y_vals_r0[0] = (pair0 >> 10) & 0x3FF;
            y_vals_r0[1] = (pair0 >> 30) & 0x3FF;
            y_vals_r0[2] = (pair1 >> 10) & 0x3FF;
            y_vals_r0[3] = (pair1 >> 30) & 0x3FF;
            y_vals_r0[4] = (pair2 >> 10) & 0x3FF;
            y_vals_r0[5] = (pair2 >> 30) & 0x3FF;
            
            u_vals_r0[0] = (pair0 >> 0) & 0x3FF;
            v_vals_r0[0] = (pair0 >> 20) & 0x3FF;
            u_vals_r0[1] = (pair1 >> 0) & 0x3FF;
            v_vals_r0[1] = (pair1 >> 20) & 0x3FF;
            u_vals_r0[2] = (pair2 >> 0) & 0x3FF;
            v_vals_r0[2] = (pair2 >> 20) & 0x3FF;
            
            /* Decode row 1 for chroma averaging */
            pair0 = (uint64_t)input_row1[(x+i+0) * 5/2 + 0] |
                    ((uint64_t)input_row1[(x+i+0) * 5/2 + 1] << 8) |
                    ((uint64_t)input_row1[(x+i+0) * 5/2 + 2] << 16) |
                    ((uint64_t)input_row1[(x+i+0) * 5/2 + 3] << 24) |
                    ((uint64_t)input_row1[(x+i+0) * 5/2 + 4] << 32);
            
            pair1 = (uint64_t)input_row1[(x+i+2) * 5/2 + 0] |
                    ((uint64_t)input_row1[(x+i+2) * 5/2 + 1] << 8) |
                    ((uint64_t)input_row1[(x+i+2) * 5/2 + 2] << 16) |
                    ((uint64_t)input_row1[(x+i+2) * 5/2 + 3] << 24) |
                    ((uint64_t)input_row1[(x+i+2) * 5/2 + 4] << 32);
            
            pair2 = (uint64_t)input_row1[(x+i+4) * 5/2 + 0] |
                    ((uint64_t)input_row1[(x+i+4) * 5/2 + 1] << 8) |
                    ((uint64_t)input_row1[(x+i+4) * 5/2 + 2] << 16) |
                    ((uint64_t)input_row1[(x+i+4) * 5/2 + 3] << 24) |
                    ((uint64_t)input_row1[(x+i+4) * 5/2 + 4] << 32);
            
            y_vals_r1[0] = (pair0 >> 10) & 0x3FF;
            y_vals_r1[1] = (pair0 >> 30) & 0x3FF;
            y_vals_r1[2] = (pair1 >> 10) & 0x3FF;
            y_vals_r1[3] = (pair1 >> 30) & 0x3FF;
            y_vals_r1[4] = (pair2 >> 10) & 0x3FF;
            y_vals_r1[5] = (pair2 >> 30) & 0x3FF;
            
            u_vals_r1[0] = (pair0 >> 0) & 0x3FF;
            v_vals_r1[0] = (pair0 >> 20) & 0x3FF;
            u_vals_r1[1] = (pair1 >> 0) & 0x3FF;
            v_vals_r1[1] = (pair1 >> 20) & 0x3FF;
            u_vals_r1[2] = (pair2 >> 0) & 0x3FF;
            v_vals_r1[2] = (pair2 >> 20) & 0x3FF;
            
            /* NEON-optimized UV averaging and packing */
            /* Load UV values into NEON registers */
            uint16x4_t u_r0 = vld1_u16(u_vals_r0);
            uint16x4_t v_r0 = vld1_u16(v_vals_r0);
            uint16x4_t u_r1 = vld1_u16(u_vals_r1);
            uint16x4_t v_r1 = vld1_u16(v_vals_r1);
            
            /* Average UV values: (a + b + 1) >> 1 */
            uint16x4_t u_avg = vhadd_u16(u_r0, u_r1);
            uint16x4_t v_avg = vhadd_u16(v_r0, v_r1);
            
            /* Extract first 3 values for packing */
            uint16_t u_avg_vals[3], v_avg_vals[3];
            vst1_u16(u_avg_vals, u_avg);
            vst1_u16(v_avg_vals, v_avg);
            
            /* Pack Y values using NEON */
            uint16x4_t y0_vec = vld1_u16(y_vals_r0);
            uint16x4_t y1_vec = vld1_u16(y_vals_r0 + 3);
            uint16x4_t y2_vec = vld1_u16(y_vals_r1);
            uint16x4_t y3_vec = vld1_u16(y_vals_r1 + 3);
            
            uint32x4_t packed_y0, packed_y1, packed_y2, packed_y3;
            pack_10bit_neon(y0_vec, y1_vec, y2_vec, &packed_y0);
            pack_10bit_neon(y3_vec, y0_vec, y1_vec, &packed_y1);
            
            /* Store packed Y values */
            int y_base = (row_pair_idx * width + x + i) / 3;
            vst1q_u32((uint32_t *)(output_y + y_base), packed_y0);
            
            /* Pack and store UV */
            uint32_t uv_packed = ((u_avg_vals[0] & 0x3FF) << 20) |
                                ((v_avg_vals[0] & 0x3FF) << 10) |
                                (u_avg_vals[1] & 0x3FF);
            output_uv[row_pair_idx * (width/2) / 3 + uv_idx++] = uv_packed;
        }
    }
    
    /* Handle remaining pixels with scalar code */
    for (; x < width - 5; x += 6) {
        uint64_t pair0, pair1, pair2;
        uint16_t y00, y01, y02, y03, y04, y05;
        uint16_t y10, y11, y12, y13, y14, y15;
        uint16_t u00, u01, u02, v00, v01, v02;
        uint16_t u10, u11, u12, v10, v11, v12;
        uint16_t u_avg0, u_avg1, u_avg2;
        uint16_t v_avg0, v_avg1, v_avg2;
        
        /* Decode 6 pixels from row 0 */
        pair0 = (uint64_t)input_row0[(x+0) * 5/2 + 0] |
                ((uint64_t)input_row0[(x+0) * 5/2 + 1] << 8) |
                ((uint64_t)input_row0[(x+0) * 5/2 + 2] << 16) |
                ((uint64_t)input_row0[(x+0) * 5/2 + 3] << 24) |
                ((uint64_t)input_row0[(x+0) * 5/2 + 4] << 32);
        
        u00 = (pair0 >> 0) & 0x3FF;
        y00 = (pair0 >> 10) & 0x3FF;
        v00 = (pair0 >> 20) & 0x3FF;
        y01 = (pair0 >> 30) & 0x3FF;
        
        pair1 = (uint64_t)input_row0[(x+2) * 5/2 + 0] |
                ((uint64_t)input_row0[(x+2) * 5/2 + 1] << 8) |
                ((uint64_t)input_row0[(x+2) * 5/2 + 2] << 16) |
                ((uint64_t)input_row0[(x+2) * 5/2 + 3] << 24) |
                ((uint64_t)input_row0[(x+2) * 5/2 + 4] << 32);
        
        u01 = (pair1 >> 0) & 0x3FF;
        y02 = (pair1 >> 10) & 0x3FF;
        v01 = (pair1 >> 20) & 0x3FF;
        y03 = (pair1 >> 30) & 0x3FF;
        
        pair2 = (uint64_t)input_row0[(x+4) * 5/2 + 0] |
                ((uint64_t)input_row0[(x+4) * 5/2 + 1] << 8) |
                ((uint64_t)input_row0[(x+4) * 5/2 + 2] << 16) |
                ((uint64_t)input_row0[(x+4) * 5/2 + 3] << 24) |
                ((uint64_t)input_row0[(x+4) * 5/2 + 4] << 32);
        
        u02 = (pair2 >> 0) & 0x3FF;
        y04 = (pair2 >> 10) & 0x3FF;
        v02 = (pair2 >> 20) & 0x3FF;
        y05 = (pair2 >> 30) & 0x3FF;
        
        /* Decode row 1 for chroma averaging */
        pair0 = (uint64_t)input_row1[(x+0) * 5/2 + 0] |
                ((uint64_t)input_row1[(x+0) * 5/2 + 1] << 8) |
                ((uint64_t)input_row1[(x+0) * 5/2 + 2] << 16) |
                ((uint64_t)input_row1[(x+0) * 5/2 + 3] << 24) |
                ((uint64_t)input_row1[(x+0) * 5/2 + 4] << 32);
        
        u10 = (pair0 >> 0) & 0x3FF;
        y10 = (pair0 >> 10) & 0x3FF;
        v10 = (pair0 >> 20) & 0x3FF;
        y11 = (pair0 >> 30) & 0x3FF;
        
        pair1 = (uint64_t)input_row1[(x+2) * 5/2 + 0] |
                ((uint64_t)input_row1[(x+2) * 5/2 + 1] << 8) |
                ((uint64_t)input_row1[(x+2) * 5/2 + 2] << 16) |
                ((uint64_t)input_row1[(x+2) * 5/2 + 3] << 24) |
                ((uint64_t)input_row1[(x+2) * 5/2 + 4] << 32);
        
        u11 = (pair1 >> 0) & 0x3FF;
        y12 = (pair1 >> 10) & 0x3FF;
        v11 = (pair1 >> 20) & 0x3FF;
        y13 = (pair1 >> 30) & 0x3FF;
        
        pair2 = (uint64_t)input_row1[(x+4) * 5/2 + 0] |
                ((uint64_t)input_row1[(x+4) * 5/2 + 1] << 8) |
                ((uint64_t)input_row1[(x+4) * 5/2 + 2] << 16) |
                ((uint64_t)input_row1[(x+4) * 5/2 + 3] << 24) |
                ((uint64_t)input_row1[(x+4) * 5/2 + 4] << 32);
        
        u12 = (pair2 >> 0) & 0x3FF;
        y14 = (pair2 >> 10) & 0x3FF;
        v12 = (pair2 >> 20) & 0x3FF;
        y15 = (pair2 >> 30) & 0x3FF;
        
        /* Average UV for 4:2:0 subsampling */
        u_avg0 = (u00 + u10 + 1) >> 1;
        v_avg0 = (v00 + v10 + 1) >> 1;
        u_avg1 = (u01 + u11 + 1) >> 1;
        v_avg1 = (v01 + v11 + 1) >> 1;
        u_avg2 = (u02 + u12 + 1) >> 1;
        v_avg2 = (v02 + v12 + 1) >> 1;
        
        /* Pack Y samples (6 Y values -> 2 words) */
        int y_base = (row_pair_idx * width + x) / 3;
        output_y[y_base + 0] = ((y00 & 0x3FF) << 20) | ((y01 & 0x3FF) << 10) | (y02 & 0x3FF);
        output_y[y_base + 1] = ((y03 & 0x3FF) << 20) | ((y04 & 0x3FF) << 10) | (y05 & 0x3FF);
        output_y[y_base + width/3 + 0] = ((y10 & 0x3FF) << 20) | ((y11 & 0x3FF) << 10) | (y12 & 0x3FF);
        output_y[y_base + width/3 + 1] = ((y13 & 0x3FF) << 20) | ((y14 & 0x3FF) << 10) | (y15 & 0x3FF);
        
        /* Pack UV samples (3 UV pairs -> 1 word) */
        output_uv[row_pair_idx * (width/2) / 3 + uv_idx] = 
            ((u_avg0 & 0x3FF) << 20) | ((v_avg0 & 0x3FF) << 10) | (u_avg1 & 0x3FF);
        output_uv[row_pair_idx * (width/2) / 3 + uv_idx + 1] = 
            ((v_avg1 & 0x3FF) << 20) | ((u_avg2 & 0x3FF) << 10) | (v_avg2 & 0x3FF);
        uv_idx += 2;
    }
}

/* Set CPU affinity based on core selection */
static void set_thread_affinity(pthread_attr_t *attr, int thread_id, CoreSelection selection)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    switch (selection) {
        case CORE_SMALL_ONLY:
            /* Small cores 0-5 */
            CPU_SET(thread_id % 6, &cpuset);
            break;
        case CORE_BIG_ONLY:
            /* Big cores 6-7 */
            CPU_SET(6 + (thread_id % 2), &cpuset);
            break;
        case CORE_BOTH:
        default:
            /* First 6 threads on small cores, remaining on big cores */
            if (thread_id < 6) {
                CPU_SET(thread_id, &cpuset);
            } else {
                CPU_SET(6 + (thread_id - 6), &cpuset);
            }
            break;
    }
    
    pthread_attr_setaffinity_np(attr, sizeof(cpuset), &cpuset);
}

/* Thread worker function */
static void *wave521_worker_thread(void *arg)
{
    Wave521Work *work = (Wave521Work *)arg;
    int row_pair;
    
    /* Process row pairs (2 input rows -> 1 UV row) */
    for (row_pair = work->start_row; row_pair < work->end_row; row_pair++) {
        const uint8_t *input_row0 = work->input + (row_pair * 2) * work->input_stride;
        const uint8_t *input_row1 = work->input + (row_pair * 2 + 1) * work->input_stride;
        
        convert_row_wave521_neon(
            input_row0,
            input_row1,
            work->output_y + row_pair * 2 * work->output_stride / 3,
            work->output_uv + row_pair * work->output_stride / 6,
            work->width,
            row_pair
        );
    }
    
    return NULL;
}

/**
 * Convert AMLOGIC YUV422 10-bit to Wave521 32-bit packed 420 10-bit
 * Multi-threaded NEON implementation with configurable CPU affinity
 */
int yuv422_10bit_convert_wave521(const ConversionParams *params)
{
    if (!params || !params->input_data || 
        !params->output_y_wave521 || !params->output_uv_wave521) {
        fprintf(stderr, "Invalid parameters\n");
        return -1;
    }
    
    int width = params->width;
    int height = params->height;
    int input_stride = (width * 5) / 2;  /* 2.5 bytes per pixel */
    int output_stride_y = (width * 4) / 3;  /* 3 samples per 4 bytes */
    int output_stride_uv = (width * 2) / 3;  /* 4:2:0 subsampling */
    
    /* Validate thread count */
    int num_threads = params->num_threads;
    if (num_threads < 1) num_threads = DEFAULT_THREADS;
    
    /* Clamp thread count based on core selection */
    switch (params->core_selection) {
        case CORE_SMALL_ONLY:
            if (num_threads > MAX_THREADS_SMALL) num_threads = MAX_THREADS_SMALL;
            break;
        case CORE_BIG_ONLY:
            if (num_threads > MAX_THREADS_BIG) num_threads = MAX_THREADS_BIG;
            break;
        case CORE_BOTH:
        default:
            if (num_threads > MAX_THREADS_BOTH) num_threads = MAX_THREADS_BOTH;
            break;
    }
    
    /* Ensure height is even for 4:2:0 */
    if (height % 2 != 0) {
        fprintf(stderr, "Height must be even for 4:2:0 format\n");
        return -1;
    }
    
    /* Ensure width is multiple of 6 for efficient processing */
    if (width % 6 != 0) {
        fprintf(stderr, "Width must be multiple of 6 for wave521 format\n");
        return -1;
    }
    
    int num_row_pairs = height / 2;
    int rows_per_thread = num_row_pairs / num_threads;
    
    pthread_t threads[num_threads];
    Wave521Work work[num_threads];
    
    /* Create threads with specified affinity */
    for (int t = 0; t < num_threads; t++) {
        work[t].thread_id = t;
        work[t].start_row = t * rows_per_thread;
        work[t].end_row = (t == num_threads - 1) ? num_row_pairs : (t + 1) * rows_per_thread;
        work[t].width = width;
        work[t].height = height;
        work[t].input = params->input_data;
        work[t].output_y = params->output_y_wave521;
        work[t].output_uv = params->output_uv_wave521;
        work[t].input_stride = input_stride;
        work[t].output_stride = output_stride_y;
        work[t].core_selection = params->core_selection;
        
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        
        /* Set CPU affinity based on core selection */
        set_thread_affinity(&attr, t, params->core_selection);
        
        pthread_create(&threads[t], &attr, wave521_worker_thread, &work[t]);
        pthread_attr_destroy(&attr);
    }
    
    /* Wait for all threads */
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }
    
    return 0;
}

/**
 * Scalar CPU implementation for wave521 format (fallback)
 */
int yuv422_10bit_convert_wave521_cpu(const ConversionParams *params)
{
    /* For now, just call the NEON version with single thread */
    ConversionParams p = *params;
    p.num_threads = 1;
    p.core_selection = CORE_SMALL_ONLY;
    return yuv422_10bit_convert_wave521(&p);
}
