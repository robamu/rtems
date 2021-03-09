/**
 * @file
 *
 * @ingroup RTEMSScoreThread
 *
 * @brief This source file contains the implementation of
 *   _Thread_Initialize().
 */

/*
 *  COPYRIGHT (c) 1989-2014.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems/score/threadimpl.h>
#include <rtems/score/freechainimpl.h>
#include <rtems/score/schedulerimpl.h>
#include <rtems/score/stackimpl.h>
#include <rtems/score/tls.h>
#include <rtems/score/userextimpl.h>
#include <rtems/score/watchdogimpl.h>
#include <rtems/config.h>

void _Thread_Free(
  Thread_Information *information,
  Thread_Control     *the_thread
)
{
#if defined(RTEMS_SMP)
  Scheduler_Node *scheduler_node;
  size_t          scheduler_index;
#endif

  _User_extensions_Thread_delete( the_thread );
  _User_extensions_Destroy_iterators( the_thread );
  _ISR_lock_Destroy( &the_thread->Keys.Lock );

#if defined(RTEMS_SMP)
  scheduler_node = the_thread->Scheduler.nodes;
  scheduler_index = 0;

  while ( scheduler_index < _Scheduler_Count ) {
    _Scheduler_Node_destroy(
      &_Scheduler_Table[ scheduler_index ],
      scheduler_node
    );
    scheduler_node = (Scheduler_Node *)
      ( (uintptr_t) scheduler_node + _Scheduler_Node_size );
    ++scheduler_index;
  }
#else
  _Scheduler_Node_destroy(
    _Thread_Scheduler_get_home( the_thread ),
    _Thread_Scheduler_get_home_node( the_thread )
  );
#endif

  _ISR_lock_Destroy( &the_thread->Timer.Lock );

  /*
   *  The thread might have been FP.  So deal with that.
   */
#if ( CPU_HARDWARE_FP == TRUE ) || ( CPU_SOFTWARE_FP == TRUE )
#if ( CPU_USE_DEFERRED_FP_SWITCH == TRUE )
  if ( _Thread_Is_allocated_fp( the_thread ) )
    _Thread_Deallocate_fp();
#endif
#endif

  _Freechain_Push(
    &information->Thread_queue_heads.Free,
    the_thread->Wait.spare_heads
  );

  /*
   *  Free the rest of the memory associated with this task
   *  and set the associated pointers to NULL for safety.
   */
  ( *the_thread->Start.stack_free )( the_thread->Start.Initial_stack.area );

#if defined(RTEMS_SMP)
  _ISR_lock_Destroy( &the_thread->Scheduler.Lock );
  _ISR_lock_Destroy( &the_thread->Wait.Lock.Default );
  _SMP_lock_Stats_destroy( &the_thread->Potpourri_stats );
#endif

  _Thread_queue_Destroy( &the_thread->Join_queue );
  _Context_Destroy( the_thread, &the_thread->Registers );
  _Objects_Free( &information->Objects, &the_thread->Object );
}

static bool _Thread_Try_initialize(
  Thread_Information         *information,
  Thread_Control             *the_thread,
  const Thread_Configuration *config
)
{
  uintptr_t                tls_size;
  size_t                   i;
  char                    *stack_begin;
  char                    *stack_end;
  uintptr_t                stack_align;
  Scheduler_Node          *scheduler_node;
#if defined(RTEMS_SMP)
  Scheduler_Node          *scheduler_node_for_index;
  const Scheduler_Control *scheduler_for_index;
  size_t                   scheduler_index;
#endif
  Per_CPU_Control         *cpu = _Per_CPU_Get_by_index( 0 );

  memset(
    &the_thread->Join_queue,
    0,
    information->Objects.object_size - offsetof( Thread_Control, Join_queue )
  );

  for ( i = 0 ; i < _Thread_Control_add_on_count ; ++i ) {
    const Thread_Control_add_on *add_on = &_Thread_Control_add_ons[ i ];

    *(void **) ( (char *) the_thread + add_on->destination_offset ) =
      (char *) the_thread + add_on->source_offset;
  }

  /* Set up the properly aligned stack area begin and end */
  stack_begin = config->stack_area;
  stack_end = stack_begin + config->stack_size;
  stack_align = CPU_STACK_ALIGNMENT;
  stack_begin = (char *) RTEMS_ALIGN_UP( (uintptr_t) stack_begin, stack_align );
  stack_end = (char *) RTEMS_ALIGN_DOWN( (uintptr_t) stack_end, stack_align );

  /* Allocate floating-point context in stack area */
#if ( CPU_HARDWARE_FP == TRUE ) || ( CPU_SOFTWARE_FP == TRUE )
  if ( config->is_fp ) {
    stack_end -= CONTEXT_FP_SIZE;
    the_thread->fp_context = (Context_Control_fp *) stack_end;
    the_thread->Start.fp_context = (Context_Control_fp *) stack_end;
  }
#endif

  tls_size = _TLS_Get_allocation_size();

  /* Allocate thread-local storage (TLS) area in stack area */
  if ( tls_size > 0 ) {
    uintptr_t tls_align;

    stack_end -= tls_size;
    tls_align = (uintptr_t) _TLS_Alignment;
    the_thread->Start.tls_area = (void *)
      ( ( (uintptr_t) stack_end + tls_align - 1 ) & ~( tls_align - 1 ) );
  }

  _Stack_Initialize(
    &the_thread->Start.Initial_stack,
    stack_begin,
    stack_end - stack_begin
  );

  /*
   *  Get thread queue heads
   */
  the_thread->Wait.spare_heads = _Freechain_Pop(
    &information->Thread_queue_heads.Free
  );
  _Thread_queue_Heads_initialize( the_thread->Wait.spare_heads );

  /*
   *  General initialization
   */

  the_thread->is_fp                  = config->is_fp;
  the_thread->Start.isr_level        = config->isr_level;
  the_thread->Start.is_preemptible   = config->is_preemptible;
  the_thread->Start.budget_algorithm = config->budget_algorithm;
  the_thread->Start.budget_callout   = config->budget_callout;
  the_thread->Start.stack_free       = config->stack_free;

  _Thread_Timer_initialize( &the_thread->Timer, cpu );

  switch ( config->budget_algorithm ) {
    case THREAD_CPU_BUDGET_ALGORITHM_NONE:
    case THREAD_CPU_BUDGET_ALGORITHM_RESET_TIMESLICE:
      break;
    #if defined(RTEMS_SCORE_THREAD_ENABLE_EXHAUST_TIMESLICE)
      case THREAD_CPU_BUDGET_ALGORITHM_EXHAUST_TIMESLICE:
        the_thread->cpu_time_budget =
          rtems_configuration_get_ticks_per_timeslice();
        break;
    #endif
    #if defined(RTEMS_SCORE_THREAD_ENABLE_SCHEDULER_CALLOUT)
      case THREAD_CPU_BUDGET_ALGORITHM_CALLOUT:
	break;
    #endif
  }

#if defined(RTEMS_SMP)
  scheduler_node = NULL;
  scheduler_node_for_index = the_thread->Scheduler.nodes;
  scheduler_for_index = &_Scheduler_Table[ 0 ];
  scheduler_index = 0;

  while ( scheduler_index < _Scheduler_Count ) {
    Priority_Control priority_for_index;

    if ( scheduler_for_index == config->scheduler ) {
      priority_for_index = config->priority;
      scheduler_node = scheduler_node_for_index;
    } else {
      /*
       * Use the idle thread priority for the non-home scheduler instances by
       * default.
       */
      priority_for_index = _Scheduler_Map_priority(
        scheduler_for_index,
        scheduler_for_index->maximum_priority
      );
    }

    _Scheduler_Node_initialize(
      scheduler_for_index,
      scheduler_node_for_index,
      the_thread,
      priority_for_index
    );
    scheduler_node_for_index = (Scheduler_Node *)
      ( (uintptr_t) scheduler_node_for_index + _Scheduler_Node_size );
    ++scheduler_for_index;
    ++scheduler_index;
  }

  _Assert( scheduler_node != NULL );
  _Chain_Initialize_one(
    &the_thread->Scheduler.Wait_nodes,
    &scheduler_node->Thread.Wait_node
  );
  _Chain_Initialize_one(
    &the_thread->Scheduler.Scheduler_nodes,
    &scheduler_node->Thread.Scheduler_node.Chain
  );
#else
  scheduler_node = _Thread_Scheduler_get_home_node( the_thread );
  _Scheduler_Node_initialize(
    config->scheduler,
    scheduler_node,
    the_thread,
    config->priority
  );
#endif

  _Priority_Node_initialize( &the_thread->Real_priority, config->priority );
  _Priority_Initialize_one(
    &scheduler_node->Wait.Priority,
    &the_thread->Real_priority
  );

#if defined(RTEMS_SMP)
  RTEMS_STATIC_ASSERT( THREAD_SCHEDULER_BLOCKED == 0, Scheduler_state );
  the_thread->Scheduler.home_scheduler = config->scheduler;
  _ISR_lock_Initialize( &the_thread->Scheduler.Lock, "Thread Scheduler" );
  _Processor_mask_Assign(
    &the_thread->Scheduler.Affinity,
    _SMP_Get_online_processors()
   );
  _ISR_lock_Initialize( &the_thread->Wait.Lock.Default, "Thread Wait Default" );
  _Thread_queue_Gate_open( &the_thread->Wait.Lock.Tranquilizer );
  _RBTree_Initialize_node( &the_thread->Wait.Link.Registry_node );
  _SMP_lock_Stats_initialize( &the_thread->Potpourri_stats, "Thread Potpourri" );
  _SMP_lock_Stats_initialize( &the_thread->Join_queue.Lock_stats, "Thread State" );
#endif

  /* Initialize the CPU for the non-SMP schedulers */
  _Thread_Set_CPU( the_thread, cpu );

  the_thread->current_state           = STATES_DORMANT;
  the_thread->Wait.operations         = &_Thread_queue_Operations_default;
  the_thread->Start.initial_priority  = config->priority;

  RTEMS_STATIC_ASSERT( THREAD_WAIT_FLAGS_INITIAL == 0, Wait_flags );

  /* POSIX Keys */
  _RBTree_Initialize_empty( &the_thread->Keys.Key_value_pairs );
  _ISR_lock_Initialize( &the_thread->Keys.Lock, "POSIX Key Value Pairs" );

  _Thread_Action_control_initialize( &the_thread->Post_switch_actions );

  _Objects_Open_u32( &information->Objects, &the_thread->Object, config->name );

  /*
   * We do following checks of simple error conditions after the thread is
   * fully initialized to simplify the clean up in case of an error.  With a
   * fully initialized thread we can simply use _Thread_Free() and do not have
   * to bother with partially initialized threads.
   */

#if defined(RTEMS_SMP)
  if (
    !config->is_preemptible
      && !_Scheduler_Is_non_preempt_mode_supported( config->scheduler )
  ) {
    return false;
  }
#endif

#if defined(RTEMS_SMP) || CPU_ENABLE_ROBUST_THREAD_DISPATCH == TRUE
  if (
    config->isr_level != 0
#if CPU_ENABLE_ROBUST_THREAD_DISPATCH == FALSE
      && _SMP_Need_inter_processor_interrupts()
#endif
  ) {
    return false;
  }
#endif

  /*
   *  We assume the Allocator Mutex is locked and dispatching is
   *  enabled when we get here.  We want to be able to run the
   *  user extensions with dispatching enabled.  The Allocator
   *  Mutex provides sufficient protection to let the user extensions
   *  run safely.
   */
  return _User_extensions_Thread_create( the_thread );
}

Status_Control _Thread_Initialize(
  Thread_Information         *information,
  Thread_Control             *the_thread,
  const Thread_Configuration *config
)
{
  bool ok;

  ok = _Thread_Try_initialize( information, the_thread, config );

  if ( !ok ) {
    _Objects_Close( &information->Objects, &the_thread->Object );
    _Thread_Free( information, the_thread );

    return STATUS_UNSATISFIED;
  }

  return STATUS_SUCCESSFUL;
}
