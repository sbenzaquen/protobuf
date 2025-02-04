// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef UPB_MINI_TABLE_INTERNAL_TAGGED_PTR_H_
#define UPB_MINI_TABLE_INTERNAL_TAGGED_PTR_H_

#include <stdint.h>

#include "upb/message/internal/message.h"

// Must be last.
#include "upb/port/def.inc"

typedef uintptr_t upb_TaggedMessagePtr;

#ifdef __cplusplus
extern "C" {
#endif

// Internal-only because empty messages cannot be created by the user.
UPB_INLINE upb_TaggedMessagePtr
UPB_PRIVATE(_upb_TaggedMessagePtr_Pack)(struct upb_Message* ptr, bool empty) {
  UPB_ASSERT(((uintptr_t)ptr & 1) == 0);
  return (uintptr_t)ptr | (empty ? 1 : 0);
}

UPB_INLINE bool UPB_PRIVATE(_upb_TaggedMessagePtr_IsEmpty)(
    upb_TaggedMessagePtr ptr) {
  return ptr & 1;
}

UPB_INLINE struct upb_Message* UPB_PRIVATE(_upb_TaggedMessagePtr_GetMessage)(
    upb_TaggedMessagePtr ptr) {
  return (struct upb_Message*)(ptr & ~(uintptr_t)1);
}

UPB_INLINE struct upb_Message* UPB_PRIVATE(
    _upb_TaggedMessagePtr_GetNonEmptyMessage)(upb_TaggedMessagePtr ptr) {
  UPB_ASSERT(!UPB_PRIVATE(_upb_TaggedMessagePtr_IsEmpty)(ptr));
  return UPB_PRIVATE(_upb_TaggedMessagePtr_GetMessage)(ptr);
}

UPB_INLINE struct upb_Message* UPB_PRIVATE(
    _upb_TaggedMessagePtr_GetEmptyMessage)(upb_TaggedMessagePtr ptr) {
  UPB_ASSERT(UPB_PRIVATE(_upb_TaggedMessagePtr_IsEmpty)(ptr));
  return UPB_PRIVATE(_upb_TaggedMessagePtr_GetMessage)(ptr);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif /* UPB_MINI_TABLE_INTERNAL_TAGGED_PTR_H_ */
