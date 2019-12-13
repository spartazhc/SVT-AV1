/*
* Copyright(c) 2019 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>
#include "EbInitialRateControlReorderQueue.h"

EbErrorType initial_rate_control_reorder_entry_ctor(
    InitialRateControlReorderEntry   *entry_ptr,
    uint32_t                          picture_number)
{
    entry_ptr->picture_number = picture_number;
    entry_ptr->parent_pcs_wrapper_ptr = (EbObjectWrapper *)EB_NULL;

    return EB_ErrorNone;
}

static void hl_rate_control_histogram_entry_dctor(EbPtr p)
{
    HlRateControlHistogramEntry* obj = (HlRateControlHistogramEntry*)p;
    EB_FREE_ARRAY(obj->me_distortion_histogram);
}

EbErrorType hl_rate_control_histogram_entry_ctor(
    HlRateControlHistogramEntry      *entry_ptr,
    uint32_t                          picture_number)
{
    entry_ptr->dctor = hl_rate_control_histogram_entry_dctor;
    entry_ptr->picture_number = picture_number;

    // ME and OIS Distortion Histograms
    EB_MALLOC(entry_ptr->me_distortion_histogram, sizeof(uint16_t) * (NUMBER_OF_SAD_INTERVALS + NUMBER_OF_INTRA_SAD_INTERVALS));
    entry_ptr->ois_distortion_histogram =  entry_ptr->me_distortion_histogram + NUMBER_OF_SAD_INTERVALS;

    return EB_ErrorNone;
}
