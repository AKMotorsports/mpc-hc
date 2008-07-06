/*
 * FFV1 codec for libavcodec
 *
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file ffv1.c
 * FF Video Codec 1 (an experimental lossless codec)
 */

#include "common.h"
#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"
#include "rangecoder.h"
#include "golomb.h"

#define MAX_PLANES 4
#define CONTEXT_SIZE 32

static const int8_t quant3[256]={
 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0,
};
static const int8_t quant5[256]={
 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-1,-1,-1,
};
static const int8_t quant7[256]={
 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-1,-1,
};
static const int8_t quant9[256]={
 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-2,-2,-2,-2,-1,-1,
};
static const int8_t quant11[256]={
 0, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-3,-3,-3,-3,-3,-3,-3,-2,-2,-2,-1,
};
static const int8_t quant13[256]={
 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,
-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,
-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,
-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,
-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-3,-3,-3,-3,-2,-2,-1,
};

static const uint8_t log2_run[32]={
 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
 4, 4, 5, 5, 6, 6, 7, 7,
 8, 9,10,11,12,13,14,15,
};

typedef struct VlcState{
    int16_t drift;
    uint16_t error_sum;
    int8_t bias;
    uint8_t count;
} VlcState;

typedef struct PlaneContext{
    int context_count;
    uint8_t (*state)[CONTEXT_SIZE];
    VlcState *vlc_state;
    uint8_t interlace_bit_state[2];
} PlaneContext;

typedef struct FFV1Context{
    AVCodecContext *avctx;
    RangeCoder c;
    GetBitContext gb;
    PutBitContext pb;
    int version;
    int width, height;
    int chroma_h_shift, chroma_v_shift;
    int flags;
    int picture_number;
    AVFrame picture;
    int plane_count;
    int ac;                              ///< 1-> CABAC 0-> golomb rice
    PlaneContext plane[MAX_PLANES];
    int16_t quant_table[5][256];
    int run_index;
    int colorspace;

    DSPContext dsp;
}FFV1Context;

static av_always_inline int fold(int diff, int bits){
    if(bits==8)
        diff= (int8_t)diff;
    else{
        diff+= 1<<(bits-1);
        diff&=(1<<bits)-1;
        diff-= 1<<(bits-1);
    }

    return diff;
}

static inline int predict(int_fast16_t *src, int_fast16_t *last){
    const int LT= last[-1];
    const int  T= last[ 0];
    const int L =  src[-1];

    return mid_pred(L, L + T - LT, T);
}

static inline int get_context(FFV1Context *f, int_fast16_t *src, int_fast16_t *last, int_fast16_t *last2){
    const int LT= last[-1];
    const int  T= last[ 0];
    const int RT= last[ 1];
    const int L =  src[-1];

    if(f->quant_table[3][127]){
        const int TT= last2[0];
        const int LL=  src[-2];
        return f->quant_table[0][(L-LT) & 0xFF] + f->quant_table[1][(LT-T) & 0xFF] + f->quant_table[2][(T-RT) & 0xFF]
              +f->quant_table[3][(LL-L) & 0xFF] + f->quant_table[4][(TT-T) & 0xFF];
    }else
        return f->quant_table[0][(L-LT) & 0xFF] + f->quant_table[1][(LT-T) & 0xFF] + f->quant_table[2][(T-RT) & 0xFF];
}

static inline void put_symbol(RangeCoder *c, uint8_t *state, int v, int is_signed){
    int i;

    if(v){
        const int a= FFABS(v);
        const int e= av_log2(a);
        put_rac(c, state+0, 0);

        assert(e<=9);

        for(i=0; i<e; i++){
            put_rac(c, state+1+i, 1);  //1..10
        }
        put_rac(c, state+1+i, 0);

        for(i=e-1; i>=0; i--){
            put_rac(c, state+22+i, (a>>i)&1); //22..31
        }

        if(is_signed)
            put_rac(c, state+11 + e, v < 0); //11..21
    }else{
        put_rac(c, state+0, 1);
    }
}

static inline int get_symbol(RangeCoder *c, uint8_t *state, int is_signed){
    if(get_rac(c, state+0))
        return 0;
    else{
        int i, e, a;
        e= 0;
        while(get_rac(c, state+1 + e)){ //1..10
            e++;
        }
        assert(e<=9);

        a= 1;
        for(i=e-1; i>=0; i--){
            a += a + get_rac(c, state+22 + i); //22..31
        }

        if(is_signed && get_rac(c, state+11 + e)) //11..21
            return -a;
        else
            return a;
    }
}

static inline void update_vlc_state(VlcState * const state, const int v){
    int drift= state->drift;
    int count= state->count;
    state->error_sum += FFABS(v);
    drift += v;

    if(count == 128){ //FIXME variable
        count >>= 1;
        drift >>= 1;
        state->error_sum >>= 1;
    }
    count++;

    if(drift <= -count){
        if(state->bias > -128) state->bias--;

        drift += count;
        if(drift <= -count)
            drift= -count + 1;
    }else if(drift > 0){
        if(state->bias <  127) state->bias++;

        drift -= count;
        if(drift > 0)
            drift= 0;
    }

    state->drift= drift;
    state->count= count;
}

static inline void put_vlc_symbol(PutBitContext *pb, VlcState * const state, int v, int bits){
    int i, k, code;
//printf("final: %d ", v);
    v = fold(v - state->bias, bits);

    i= state->count;
    k=0;
    while(i < state->error_sum){ //FIXME optimize
        k++;
        i += i;
    }

    assert(k<=8);

#if 0 // JPEG LS
    if(k==0 && 2*state->drift <= - state->count) code= v ^ (-1);
    else                                         code= v;
#else
     code= v ^ ((2*state->drift + state->count)>>31);
#endif

//printf("v:%d/%d bias:%d error:%d drift:%d count:%d k:%d\n", v, code, state->bias, state->error_sum, state->drift, state->count, k);
    set_sr_golomb(pb, code, k, 12, bits);

    update_vlc_state(state, v);
}

static inline int get_vlc_symbol(GetBitContext *gb, VlcState * const state, int bits){
    int k, i, v, ret;

    i= state->count;
    k=0;
    while(i < state->error_sum){ //FIXME optimize
        k++;
        i += i;
    }

    assert(k<=8);

    v= get_sr_golomb(gb, k, 12, bits);
//printf("v:%d bias:%d error:%d drift:%d count:%d k:%d", v, state->bias, state->error_sum, state->drift, state->count, k);

#if 0 // JPEG LS
    if(k==0 && 2*state->drift <= - state->count) v ^= (-1);
#else
     v ^= ((2*state->drift + state->count)>>31);
#endif

    ret= fold(v + state->bias, bits);

    update_vlc_state(state, v);
//printf("final: %d\n", ret);
    return ret;
}

static av_cold int common_init(AVCodecContext *avctx){
    FFV1Context *s = avctx->priv_data;
    int width, height;

    s->avctx= avctx;
    s->flags= avctx->flags;

    dsputil_init(&s->dsp, avctx);

    width= s->width= avctx->width;
    height= s->height= avctx->height;

    assert(width && height);

    return 0;
}

static void clear_state(FFV1Context *f){
    int i, j;

    for(i=0; i<f->plane_count; i++){
        PlaneContext *p= &f->plane[i];

        p->interlace_bit_state[0]= 128;
        p->interlace_bit_state[1]= 128;

        for(j=0; j<p->context_count; j++){
            if(f->ac){
                memset(p->state[j], 128, sizeof(uint8_t)*CONTEXT_SIZE);
            }else{
                p->vlc_state[j].drift= 0;
                p->vlc_state[j].error_sum= 4; //FFMAX((RANGE + 32)/64, 2);
                p->vlc_state[j].bias= 0;
                p->vlc_state[j].count= 1;
            }
        }
    }
}

static av_cold int common_end(AVCodecContext *avctx){
    FFV1Context *s = avctx->priv_data;
    int i;

    for(i=0; i<s->plane_count; i++){
        PlaneContext *p= &s->plane[i];

        av_freep(&p->state);
        av_freep(&p->vlc_state);
    }

    return 0;
}

static inline void decode_line(FFV1Context *s, int w, int_fast16_t *sample[2], int plane_index, int bits){
    PlaneContext * const p= &s->plane[plane_index];
    RangeCoder * const c= &s->c;
    int x;
    int run_count=0;
    int run_mode=0;
    int run_index= s->run_index;

    for(x=0; x<w; x++){
        int diff, context, sign;

        context= get_context(s, sample[1] + x, sample[0] + x, sample[1] + x);
        if(context < 0){
            context= -context;
            sign=1;
        }else
            sign=0;


        if(s->ac){
            diff= get_symbol(c, p->state[context], 1);
        }else{
            if(context == 0 && run_mode==0) run_mode=1;

            if(run_mode){
                if(run_count==0 && run_mode==1){
                    if(get_bits1(&s->gb)){
                        run_count = 1<<log2_run[run_index];
                        if(x + run_count <= w) run_index++;
                    }else{
                        if(log2_run[run_index]) run_count = get_bits(&s->gb, log2_run[run_index]);
                        else run_count=0;
                        if(run_index) run_index--;
                        run_mode=2;
                    }
                }
                run_count--;
                if(run_count < 0){
                    run_mode=0;
                    run_count=0;
                    diff= get_vlc_symbol(&s->gb, &p->vlc_state[context], bits);
                    if(diff>=0) diff++;
                }else
                    diff=0;
            }else
                diff= get_vlc_symbol(&s->gb, &p->vlc_state[context], bits);

//            printf("count:%d index:%d, mode:%d, x:%d y:%d pos:%d\n", run_count, run_index, run_mode, x, y, get_bits_count(&s->gb));
        }

        if(sign) diff= -diff;

        sample[1][x]= (predict(sample[1] + x, sample[0] + x) + diff) & ((1<<bits)-1);
    }
    s->run_index= run_index;
}

static void decode_plane(FFV1Context *s, uint8_t *src, int w, int h, int stride, int plane_index){
    int x, y;
#if __STDC_VERSION__ >= 199901L
    int_fast16_t sample_buffer[2][w+6];
#else
    int_fast16_t *sample_buffer;
#endif
    int_fast16_t *sample[2]= {sample_buffer[0]+3, sample_buffer[1]+3};

    s->run_index=0;

    memset(sample_buffer, 0, sizeof(sample_buffer));

    for(y=0; y<h; y++){
        int_fast16_t *temp= sample[0]; //FIXME try a normal buffer

        sample[0]= sample[1];
        sample[1]= temp;

        sample[1][-1]= sample[0][0  ];
        sample[0][ w]= sample[0][w-1];

//{START_TIMER
        decode_line(s, w, sample, plane_index, 8);
        for(x=0; x<w; x++){
            src[x + stride*y]= sample[1][x];
        }
//STOP_TIMER("decode-line")}
    }
}

static void decode_rgb_frame(FFV1Context *s, uint32_t *src, int w, int h, int stride){
    int x, y, p;
#if __STDC_VERSION__ >= 199901L
    int_fast16_t sample_buffer[3][2][w+6];
#else
    int_fast16_t *sample_buffer[3][2];
#endif
    int_fast16_t *sample[3][2]= {
        {sample_buffer[0][0]+3, sample_buffer[0][1]+3},
        {sample_buffer[1][0]+3, sample_buffer[1][1]+3},
        {sample_buffer[2][0]+3, sample_buffer[2][1]+3}};

    s->run_index=0;

    memset(sample_buffer, 0, sizeof(sample_buffer));

    for(y=0; y<h; y++){
        for(p=0; p<3; p++){
            int_fast16_t *temp= sample[p][0]; //FIXME try a normal buffer

            sample[p][0]= sample[p][1];
            sample[p][1]= temp;

            sample[p][1][-1]= sample[p][0][0  ];
            sample[p][0][ w]= sample[p][0][w-1];
            decode_line(s, w, sample[p], FFMIN(p, 1), 9);
        }
        for(x=0; x<w; x++){
            int g= sample[0][1][x];
            int b= sample[1][1][x];
            int r= sample[2][1][x];

//            assert(g>=0 && b>=0 && r>=0);
//            assert(g<256 && b<512 && r<512);

            b -= 0x100;
            r -= 0x100;
            g -= (b + r)>>2;
            b += g;
            r += g;

            src[x + stride*y]= b + (g<<8) + (r<<16);
        }
    }
}

static int read_quant_table(RangeCoder *c, int16_t *quant_table, int scale){
    int v;
    int i=0;
    uint8_t state[CONTEXT_SIZE];

    memset(state, 128, sizeof(state));

    for(v=0; i<128 ; v++){
        int len= get_symbol(c, state, 0) + 1;

        if(len + i > 128) return -1;

        while(len--){
            quant_table[i] = scale*v;
            i++;
//printf("%2d ",v);
//if(i%16==0) printf("\n");
        }
    }

    for(i=1; i<128; i++){
        quant_table[256-i]= -quant_table[i];
    }
    quant_table[128]= -quant_table[127];

    return 2*v - 1;
}

static int read_header(FFV1Context *f){
    uint8_t state[CONTEXT_SIZE];
    int i, context_count;
    RangeCoder * const c= &f->c;

    memset(state, 128, sizeof(state));

    f->version= get_symbol(c, state, 0);
    f->ac= f->avctx->coder_type= get_symbol(c, state, 0);
    f->colorspace= get_symbol(c, state, 0); //YUV cs type
    get_rac(c, state); //no chroma = false
    f->chroma_h_shift= get_symbol(c, state, 0);
    f->chroma_v_shift= get_symbol(c, state, 0);
    get_rac(c, state); //transparency plane
    f->plane_count= 2;

    if(f->colorspace==0){
        switch(16*f->chroma_h_shift + f->chroma_v_shift){
        case 0x00: f->avctx->pix_fmt= PIX_FMT_YUV444P; break;
        case 0x10: f->avctx->pix_fmt= PIX_FMT_YUV422P; break;
        case 0x11: f->avctx->pix_fmt= PIX_FMT_YUV420P; break;
        case 0x20: f->avctx->pix_fmt= PIX_FMT_YUV411P; break;
        case 0x22: f->avctx->pix_fmt= PIX_FMT_YUV410P; break;
        default:
            av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
            return -1;
        }
    }else if(f->colorspace==1){
        if(f->chroma_h_shift || f->chroma_v_shift){
            av_log(f->avctx, AV_LOG_ERROR, "chroma subsampling not supported in this colorspace\n");
            return -1;
        }
        f->avctx->pix_fmt= PIX_FMT_RGB32;
    }else{
        av_log(f->avctx, AV_LOG_ERROR, "colorspace not supported\n");
        return -1;
    }

//printf("%d %d %d\n", f->chroma_h_shift, f->chroma_v_shift,f->avctx->pix_fmt);

    context_count=1;
    for(i=0; i<5; i++){
        context_count*= read_quant_table(c, f->quant_table[i], context_count);
        if(context_count < 0 || context_count > 32768){
            av_log(f->avctx, AV_LOG_ERROR, "read_quant_table error\n");
            return -1;
        }
    }
    context_count= (context_count+1)/2;

    for(i=0; i<f->plane_count; i++){
        PlaneContext * const p= &f->plane[i];

        p->context_count= context_count;

        if(f->ac){
            if(!p->state) p->state= av_malloc(CONTEXT_SIZE*p->context_count*sizeof(uint8_t));
        }else{
            if(!p->vlc_state) p->vlc_state= av_malloc(p->context_count*sizeof(VlcState));
        }
    }

    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
//    FFV1Context *s = avctx->priv_data;

    common_init(avctx);

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, const uint8_t *buf, int buf_size){
    FFV1Context *f = avctx->priv_data;
    RangeCoder * const c= &f->c;
    const int width= f->width;
    const int height= f->height;
    AVFrame * const p= &f->picture;
    int bytes_read;
    uint8_t keystate= 128;

    AVFrame *picture = data;

    ff_init_range_decoder(c, buf, buf_size);
    ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);


    p->pict_type= FF_I_TYPE; //FIXME I vs. P
    if(get_rac(c, &keystate)){
        p->key_frame= 1;
        if(read_header(f) < 0)
            return -1;
        clear_state(f);
    }else{
        p->key_frame= 0;
    }
    if(!f->plane[0].state && !f->plane[0].vlc_state)
        return -1;

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    if(avctx->debug&FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_ERROR, "keyframe:%d coder:%d\n", p->key_frame, f->ac);

    if(!f->ac){
        bytes_read = c->bytestream - c->bytestream_start - 1;
        if(bytes_read ==0) av_log(avctx, AV_LOG_ERROR, "error at end of AC stream\n"); //FIXME
//printf("pos=%d\n", bytes_read);
        init_get_bits(&f->gb, buf + bytes_read, buf_size - bytes_read);
    } else {
        bytes_read = 0; /* avoid warning */
    }

    if(f->colorspace==0){
        const int chroma_width = -((-width )>>f->chroma_h_shift);
        const int chroma_height= -((-height)>>f->chroma_v_shift);
        decode_plane(f, p->data[0], width, height, p->linesize[0], 0);

        decode_plane(f, p->data[1], chroma_width, chroma_height, p->linesize[1], 1);
        decode_plane(f, p->data[2], chroma_width, chroma_height, p->linesize[2], 1);
    }else{
        decode_rgb_frame(f, (uint32_t*)p->data[0], width, height, p->linesize[0]/4);
    }

    emms_c();

    f->picture_number++;

    *picture= *p;

    avctx->release_buffer(avctx, p); //FIXME

    *data_size = sizeof(AVFrame);

    if(f->ac){
        bytes_read= c->bytestream - c->bytestream_start - 1;
        if(bytes_read ==0) av_log(f->avctx, AV_LOG_ERROR, "error at end of frame\n");
    }else{
        bytes_read+= (get_bits_count(&f->gb)+7)/8;
    }

    return bytes_read;
}

AVCodec ffv1_decoder = {
    "ffv1",
    CODEC_TYPE_VIDEO,
    CODEC_ID_FFV1,
    sizeof(FFV1Context),
    decode_init,
    NULL,
    common_end,
    decode_frame,
    CODEC_CAP_DR1 /*| CODEC_CAP_DRAW_HORIZ_BAND*/,
    /*.next = */NULL,
    /*.flush = */NULL,
    /*.supported_framerates = */NULL,
    /*.pix_fmts = */NULL,
    /*.long_name= */"FFmpeg codec #1",
};
