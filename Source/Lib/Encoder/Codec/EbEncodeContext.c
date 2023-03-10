/*
* Copyright(c) 2019 Intel Corporation
*
* This source code is subject to the terms of the BSD 2 Clause License and
* the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/

#include <stdlib.h>

#include "EbEncodeContext.h"
#include "EbSvtAv1ErrorCodes.h"
#include "EbThreads.h"

static EbErrorType create_stats_buffer(FIRSTPASS_STATS **frame_stats_buffer,
                                       STATS_BUFFER_CTX *stats_buf_context, int num_lap_buffers) {
    EbErrorType res = EB_ErrorNone;

    int size = get_stats_buf_size(num_lap_buffers, MAX_LAG_BUFFERS);
    // *frame_stats_buffer =
    //     (FIRSTPASS_STATS *)aom_calloc(size, sizeof(FIRSTPASS_STATS));
    EB_MALLOC_ARRAY((*frame_stats_buffer), size);
    if (*frame_stats_buffer == NULL)
        return EB_ErrorInsufficientResources;

    stats_buf_context->stats_in_start     = *frame_stats_buffer;
    stats_buf_context->stats_in_end_write = stats_buf_context->stats_in_start;
    stats_buf_context->stats_in_end       = stats_buf_context->stats_in_start;
    stats_buf_context->stats_in_buf_end   = stats_buf_context->stats_in_start + size;

    EB_MALLOC_ARRAY(stats_buf_context->total_left_stats, 1);
    if (stats_buf_context->total_left_stats == NULL)
        return EB_ErrorInsufficientResources;
    svt_av1_twopass_zero_stats(stats_buf_context->total_left_stats);
    EB_MALLOC_ARRAY(stats_buf_context->total_stats, 1);
    if (stats_buf_context->total_stats == NULL)
        return EB_ErrorInsufficientResources;
    svt_av1_twopass_zero_stats(stats_buf_context->total_stats);
    stats_buf_context->last_frame_accumulated = -1;

    EB_CREATE_MUTEX(stats_buf_context->stats_in_write_mutex);
    return res;
}

static void destroy_stats_buffer(STATS_BUFFER_CTX *stats_buf_context,
                                 FIRSTPASS_STATS  *frame_stats_buffer) {
    EB_FREE_ARRAY(stats_buf_context->total_left_stats);
    EB_FREE_ARRAY(stats_buf_context->total_stats);
    EB_FREE_ARRAY(frame_stats_buffer);
    EB_DESTROY_MUTEX(stats_buf_context->stats_in_write_mutex);
}
static void encode_context_dctor(EbPtr p) {
    EncodeContext *obj = (EncodeContext *)p;
    EB_DESTROY_MUTEX(obj->total_number_of_recon_frame_mutex);
    EB_DESTROY_MUTEX(obj->sc_buffer_mutex);
    EB_DESTROY_MUTEX(obj->stat_file_mutex);
    EB_DESTROY_MUTEX(obj->frame_updated_mutex);
    EB_DELETE(obj->prediction_structure_group_ptr);
    EB_DELETE_PTR_ARRAY(obj->picture_decision_reorder_queue,
                        PICTURE_DECISION_REORDER_QUEUE_MAX_DEPTH);
    EB_FREE(obj->pre_assignment_buffer);
    EB_DELETE_PTR_ARRAY(obj->input_picture_queue, INPUT_QUEUE_MAX_DEPTH);
    EB_DELETE_PTR_ARRAY(obj->reference_picture_list, obj->reference_picture_list_length);
    EB_DELETE_PTR_ARRAY(obj->pd_dpb, REF_FRAMES);
    EB_DELETE_PTR_ARRAY(obj->initial_rate_control_reorder_queue,
                        INITIAL_RATE_CONTROL_REORDER_QUEUE_MAX_DEPTH);
    EB_DELETE_PTR_ARRAY(obj->packetization_reorder_queue, PACKETIZATION_REORDER_QUEUE_MAX_DEPTH);
    EB_FREE(obj->stats_out.stat);
    destroy_stats_buffer(&obj->stats_buf_context, obj->frame_stats_buffer);
    EB_DELETE_PTR_ARRAY(obj->rc.coded_frames_stat_queue, CODED_FRAMES_STAT_QUEUE_MAX_DEPTH);

    if (obj->rc_param_queue)
        EB_FREE_2D(obj->rc_param_queue);
    EB_DESTROY_MUTEX(obj->rc_param_queue_mutex);
    EB_DESTROY_MUTEX(obj->rc.rc_mutex);
}

EbErrorType encode_context_ctor(EncodeContext *encode_context_ptr, EbPtr object_init_data_ptr) {
    uint32_t picture_index;

    encode_context_ptr->dctor = encode_context_dctor;

    (void)object_init_data_ptr;
    CHECK_REPORT_ERROR(1, encode_context_ptr->app_callback_ptr, EB_ENC_EC_ERROR29);

    EB_CREATE_MUTEX(encode_context_ptr->total_number_of_recon_frame_mutex);
    EB_CREATE_MUTEX(encode_context_ptr->frame_updated_mutex);
    EB_ALLOC_PTR_ARRAY(encode_context_ptr->picture_decision_reorder_queue,
                       PICTURE_DECISION_REORDER_QUEUE_MAX_DEPTH);

    for (picture_index = 0; picture_index < PICTURE_DECISION_REORDER_QUEUE_MAX_DEPTH;
         ++picture_index) {
        EB_NEW(encode_context_ptr->picture_decision_reorder_queue[picture_index],
               picture_decision_reorder_entry_ctor,
               picture_index);
    }
    EB_ALLOC_PTR_ARRAY(encode_context_ptr->pre_assignment_buffer, PRE_ASSIGNMENT_MAX_DEPTH);

    EB_ALLOC_PTR_ARRAY(encode_context_ptr->input_picture_queue, INPUT_QUEUE_MAX_DEPTH);

    for (picture_index = 0; picture_index < INPUT_QUEUE_MAX_DEPTH; ++picture_index) {
        EB_NEW(encode_context_ptr->input_picture_queue[picture_index], input_queue_entry_ctor);
    }

    EB_ALLOC_PTR_ARRAY(encode_context_ptr->pd_dpb, REF_FRAMES);
    for (picture_index = 0; picture_index < REF_FRAMES; ++picture_index) {
        EB_NEW(encode_context_ptr->pd_dpb[picture_index], pa_reference_queue_entry_ctor);
    }
    EB_ALLOC_PTR_ARRAY(encode_context_ptr->initial_rate_control_reorder_queue,
                       INITIAL_RATE_CONTROL_REORDER_QUEUE_MAX_DEPTH);

    for (picture_index = 0; picture_index < INITIAL_RATE_CONTROL_REORDER_QUEUE_MAX_DEPTH;
         ++picture_index) {
        EB_NEW(encode_context_ptr->initial_rate_control_reorder_queue[picture_index],
               initial_rate_control_reorder_entry_ctor,
               picture_index);
    }

    EB_ALLOC_PTR_ARRAY(encode_context_ptr->packetization_reorder_queue,
                       PACKETIZATION_REORDER_QUEUE_MAX_DEPTH);

    for (picture_index = 0; picture_index < PACKETIZATION_REORDER_QUEUE_MAX_DEPTH;
         ++picture_index) {
        EB_NEW(encode_context_ptr->packetization_reorder_queue[picture_index],
               packetization_reorder_entry_ctor,
               picture_index);
    }

    encode_context_ptr->initial_picture = TRUE;

    // Sequence Termination Flags
    encode_context_ptr->terminating_picture_number = ~0u;

    EB_CREATE_MUTEX(encode_context_ptr->sc_buffer_mutex);
    encode_context_ptr->enc_mode         = SPEED_CONTROL_INIT_MOD;
    encode_context_ptr->recode_tolerance = 25;
    encode_context_ptr->rc_cfg.min_cr    = 0;
    EB_CREATE_MUTEX(encode_context_ptr->stat_file_mutex);
    encode_context_ptr->num_lap_buffers = 0; //lap not supported for now
    int *num_lap_buffers                = &encode_context_ptr->num_lap_buffers;
    create_stats_buffer(&encode_context_ptr->frame_stats_buffer,
                        &encode_context_ptr->stats_buf_context,
                        *num_lap_buffers);
    EB_ALLOC_PTR_ARRAY(encode_context_ptr->rc.coded_frames_stat_queue,
                       CODED_FRAMES_STAT_QUEUE_MAX_DEPTH);

    EB_CREATE_MUTEX(encode_context_ptr->rc.rc_mutex);
    for (picture_index = 0; picture_index < CODED_FRAMES_STAT_QUEUE_MAX_DEPTH; ++picture_index) {
        EB_NEW(encode_context_ptr->rc.coded_frames_stat_queue[picture_index],
               rate_control_coded_frames_stats_context_ctor,
               picture_index);
    }
    encode_context_ptr->rc.min_bit_actual_per_gop = 0xfffffffffffff;
    EB_MALLOC_2D(encode_context_ptr->rc_param_queue, (int32_t)PARALLEL_GOP_MAX_NUMBER, 1);

    for (int interval_index = 0; interval_index < PARALLEL_GOP_MAX_NUMBER; interval_index++) {
        encode_context_ptr->rc_param_queue[interval_index]->first_poc                = 0;
        encode_context_ptr->rc_param_queue[interval_index]->processed_frame_number   = 0;
        encode_context_ptr->rc_param_queue[interval_index]->size                     = -1;
        encode_context_ptr->rc_param_queue[interval_index]->end_of_seq_seen          = 0;
        encode_context_ptr->rc_param_queue[interval_index]->last_i_qp                = 0;
        encode_context_ptr->rc_param_queue[interval_index]->vbr_bits_off_target      = 0;
        encode_context_ptr->rc_param_queue[interval_index]->vbr_bits_off_target_fast = 0;
        encode_context_ptr->rc_param_queue[interval_index]->rolling_target_bits =
            encode_context_ptr->rc.avg_frame_bandwidth;
        encode_context_ptr->rc_param_queue[interval_index]->rolling_actual_bits =
            encode_context_ptr->rc.avg_frame_bandwidth;
        encode_context_ptr->rc_param_queue[interval_index]->rate_error_estimate = 0;
        encode_context_ptr->rc_param_queue[interval_index]->total_actual_bits   = 0;
        encode_context_ptr->rc_param_queue[interval_index]->total_target_bits   = 0;
        encode_context_ptr->rc_param_queue[interval_index]->extend_minq         = 0;
        encode_context_ptr->rc_param_queue[interval_index]->extend_maxq         = 0;
        encode_context_ptr->rc_param_queue[interval_index]->extend_minq_fast    = 0;
    }
    encode_context_ptr->rc_param_queue_head_index = 0;
    EB_CREATE_MUTEX(encode_context_ptr->rc_param_queue_mutex);

    return EB_ErrorNone;
}
