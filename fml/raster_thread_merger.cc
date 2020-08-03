// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define FML_USED_ON_EMBEDDER

#include "flutter/fml/raster_thread_merger.h"
#include "flutter/fml/message_loop_impl.h"

namespace fml {

const int RasterThreadMerger::kLeaseNotSet = -1;

RasterThreadMerger::RasterThreadMerger(fml::TaskQueueId platform_queue_id,
                                       fml::TaskQueueId gpu_queue_id)
    : platform_queue_id_(platform_queue_id),
      gpu_queue_id_(gpu_queue_id),
      task_queues_(fml::MessageLoopTaskQueues::GetInstance()),
      lease_term_(kLeaseNotSet) {
  bool is_merged = task_queues_->Owns(platform_queue_id_, gpu_queue_id_);
  is_merged_.store(is_merged);
  if (is_merged) {
    lease_term_ = 1;
  }
}

void RasterThreadMerger::MergeWithLease(size_t lease_term) {
  FML_DCHECK(lease_term > 0) << "lease_term should be positive.";
  if (!is_merged_.load()) {
    FML_DLOG(ERROR) << "--- tm_: merged";
    is_merged_.store(task_queues_->Merge(platform_queue_id_, gpu_queue_id_));
    lease_term_ = lease_term;
  }
}

void RasterThreadMerger::UnMergeNow() {
  FML_CHECK(IsOnRasterizingThread());
  lease_term_ = 0;
  bool success = task_queues_->Unmerge(platform_queue_id_);
  FML_CHECK(success) << "Unable to un-merge the raster and platform threads.";
  is_merged_.store(false);
  FML_DLOG(ERROR) << "--- tm_: unmerged";
}

bool RasterThreadMerger::IsOnPlatformThread() const {
  return MessageLoop::GetCurrentTaskQueueId() == platform_queue_id_;
}

bool RasterThreadMerger::IsOnRasterizingThread() const {
  if (is_merged_.load()) {
    return IsOnPlatformThread();
  } else {
    return !IsOnPlatformThread();
  }
}

void RasterThreadMerger::ExtendLeaseTo(size_t lease_term) {
  FML_DCHECK(lease_term > 0) << "lease_term should be positive.";
  if (lease_term_ != kLeaseNotSet &&
      static_cast<int>(lease_term) > lease_term_) {
    lease_term_ = lease_term;
  }
}

bool RasterThreadMerger::IsMerged() const {
  return is_merged_.load();
}

void RasterThreadMerger::WaitUntilMerged() {
  FML_CHECK(MessageLoop::GetCurrentTaskQueueId() != gpu_queue_id_);
  while (!is_merged_.load())
    ;
}

RasterThreadStatus RasterThreadMerger::DecrementLease() {
  if (!is_merged_.load()) {
    return RasterThreadStatus::kRemainsUnmerged;
  }

  // we haven't been set to merge.
  if (lease_term_ == kLeaseNotSet) {
    return RasterThreadStatus::kRemainsUnmerged;
  }

  FML_DCHECK(lease_term_ > 0)
      << "lease_term should always be positive when merged.";
  lease_term_--;
  if (lease_term_ == 0) {
    UnMergeNow();
    return RasterThreadStatus::kUnmergedNow;
  }

  return RasterThreadStatus::kRemainsMerged;
}

}  // namespace fml
