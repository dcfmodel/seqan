// ==========================================================================
//                 SeqAn - The Library for Sequence Analysis
// ==========================================================================
// Copyright (c) 2006-2016, Knut Reinert, FU Berlin
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of Knut Reinert or the FU Berlin nor the names of
//       its contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL KNUT REINERT OR THE FU BERLIN BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
//
// ==========================================================================
// Author: Rene Rahn <rene.rahn@fu-berlin.de>
// ==========================================================================
//  Implements the new interface for calling alingment algorithms.
// ==========================================================================

#ifndef INCLUDE_SEQAN_ALIGN_PARALLEL_ALIGN_INTERFACE_H_
#define INCLUDE_SEQAN_ALIGN_PARALLEL_ALIGN_INTERFACE_H_

namespace seqan {
namespace impl {
// ============================================================================
// Forwards
// ============================================================================

// ============================================================================
// Tags, Classes, Enums
// ============================================================================

struct ScalarWorker
{

    template <typename TQueueContext>
    inline void
    operator()(TQueueContext & queueContext)
    {
        lockWriting(queueContext.mQueue);
        while (true)
        {
            auto task = popFront(queueContext.mQueue);

            if (task == nullptr)
                return;

            SEQAN_ASSERT(task != nullptr);
            task->template execute(queueContext.mQueue, Nothing());
        }
    }
};

struct SimdWorker
{
    template <typename TQueueContext>
    inline void
    operator()(TQueueContext & queueContext)
    {
        using TTask = decltype(popFront(queueContext.mQueue));

        lockWriting(queueContext.mQueue);
        std::vector<TTask> tasks;
        while (true)
        {
            TTask task = nullptr;
            tasks.clear();
            {
                std::lock_guard<decltype(queueContext.mLock)> scopedLock(queueContext.mLock);
                task = popFront(queueContext.mQueue);
                if (task == nullptr)
                    return;

                if (length(queueContext.mQueue) >= TQueueContext::VECTOR_SIZE - 1)
                {
                    for (unsigned i = 0; i < TQueueContext::VECTOR_SIZE - 1; ++i)
                        tasks.push_back(popFront(*workQueuePtr));
                }
            }

            SEQAN_ASSERT(task != nullptr);
            task->template execute(*workQueuePtr, tasks, mThreadId);
        }
    }
};

// ============================================================================
// Metafunctions
// ============================================================================

// ============================================================================
// Functions
// ============================================================================

template <typename TParallelPolicy, typename TSchedulingPolicy>
struct BatchAlignmentExecutor;

template <typename TParSpec, typename TSchedulingSpec>
struct BatchAlignmentExecutor<Parallel<TSpec>, Dynamic<TSchedulingSpec>>
{
    template <typename TContext,
              typename TSeqBatchH,
              typename TSeqBatchV,
              typename TDelegate>
    inline static void run(TContext const & context,
                           TSeqBatchH const & seqBatchH,
                           TSeqBatchV const & seqBatchV,
                           TDelegate && delegate)
    {
        // We can nothing say about the parallel structure.
        // We might want to choose a different approach to get the result type.
        // This can be achieved by defining the IntermediateDPResult by a the context traits.

        // TODO(rrahn): Need to update when using more information.
        using TIntermediate = IntermediateDPResult<typename TTContext::TScout>;
        using TEnumerableThreadSpecific = EnumerableThreadSpecific<TIntermediate>;  // Can be switched between TBB, and own implementation.

        // Now we need to create an TEnumerableThreadSpecific object.
        TEnumerableThreadSpecific ets;

        // Now we need to create the ParallelContext.
        // We need a thread pool.
        ThreadPool pool;

        using TTask = DPTaskImpl<TTaskContext, TThreadLocalStorage, TVecExecPolicy, ParallelExecutionPolicyNative>;
        using TWorkQueue = ConcurrentQueue<TTask *>;
        using TDPThread = DPThread<TWorkQueue, TTaskContext, typename IsVectorExecutionPolicy<TVecExecPolicy>::Type>;

        using TQueueContext = 
        TQueueContext queueContext;

        // Prepare the tls.
        std::mutex mtx;
        // We need a callable with the parameters for
        // We can start the pool here.
        // SimdSwitch
        for (unsigned i = 0; i < numParallelWorkers(context); ++i)
            emplaceBack(pool, ScalarWorker, std::ref(queueContext));

        waitForWriters(queueContext.mQueue, numParallelWorkers(context));
        // Now we need to define the context that runs in every AlingmentInstance.
        // This context needs a reference to the thread_pool
        // This context needs a reference to the ets
        // This context needs a reference to the sequences used.
        // For this we need the callable function.

        ParallelDPAlignmentContext<Parallel<TSpec> >           // Defines the thread pool, the tasks, buffer and thread_local storage and so on ...
        
        // Normally we would create a single AlingmentInstance and call it.
        // We get the traits for the alingment instance and we need to add a policy for the alignment instance.
        // All these parameters depend on certain trait values.
        
        AlignmentInstance<ParallelDPAlignmentContext> parallelDpContext;
        
        // We need to set the number of concurrent Alignments running
        // We need to propagate the dpTaskPool to the underlying structures.
        
        
        using TSchedulerTraits = typename Traits<Dynamic<TSchedulingPolicy>>::Type;
        AlignmentScheduler<AICallableRAIIWrapper<TAlignmentInstance>, TSchedulerTraits> scheduler();
        
        using TSeqH = typename Value<TSequenceH>::Type;
        using TSeqV = typename Value<TSequenceV>::Type;
        
        auto zip_cont = make_zip(seqSetH, seqSetV);
        
        for (std::tie<seqH, seqV> tie : zip_cont)
        {
            // Here we call asynchronsly into a concurrent queue.
            // TODO(rrahn): Lazy thread creation mechanism.
            // Is supposed to be a callable.
            // Recycling, becuase we don't allocate more than currently consumed by the list
            schedule(scheduler, impl::AICallableRaiiWrapper<TAlignmentInstance>{seqH, seqV, config, delegate});
        }
    }
};

// Now we can implement different strategies to compute the alignment.
template <typename TScore, typename TDPTraits, typename TExecutionTraits,
          typename ...Ts>
void align_batch(DPConfig<TScore, TDPTraits, TExecutionTraits> const & config,
                 Ts ...&& args)
{
    BatchAlignmentExecutor<typename TExecutionTraits::TParallelPolixy, typename TExecutionTraits::TSchedulingPolicy>::run(config, std::forward<Ts>(args)...);
}

}  // namespace impl

}  // namespace seqan

#endif  // #ifndef INCLUDE_SEQAN_ALIGN_PARALLEL_ALIGN_INTERFACE_H_
