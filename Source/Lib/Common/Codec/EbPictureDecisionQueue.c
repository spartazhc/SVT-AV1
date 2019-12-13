/*
* Copyright(c) 2019 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <stdlib.h>
#include "EbPictureDecisionQueue.h"

static void pa_reference_queue_entry_dctor(EbPtr p)
{
    PaReferenceQueueEntry* obj = (PaReferenceQueueEntry*)p;
    EB_FREE(obj->list0.list);
}

EbErrorType pa_reference_queue_entry_ctor(
    PaReferenceQueueEntry   *entryPtr)
{
    entryPtr->dctor = pa_reference_queue_entry_dctor;
    EB_MALLOC(entryPtr->list0.list, 2 * sizeof(int32_t) * (1 << MAX_TEMPORAL_LAYERS));
    entryPtr->list1.list =  entryPtr->list0.list + (1 << MAX_TEMPORAL_LAYERS);

    return EB_ErrorNone;
}
