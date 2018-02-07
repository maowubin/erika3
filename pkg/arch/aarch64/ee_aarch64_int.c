/* ###*B*###
 * ERIKA Enterprise - a tiny RTOS for small microcontrollers
 *
 * Copyright (C) 2002-2017 Evidence Srl
 *
 * This file is part of ERIKA Enterprise.
 *
 * See LICENSE file.
 * ###*E*### */

/** \file   ee_aarch64_int.c
 *  \brief  Interrupt configuration.
 *
 *  This files contains the interrupt configuration implementation for
 *  a specific Architecture in Erika Enterprise.
 *
 *  \author  Errico Gudiieri
 *  \date    2017
 */

#include "ee_internal.h"

OsEE_aarch64_hnd_type
  osEE_aarch64_ppi_isr_vectors[OSEE_USED_CORES][OSEE_GIC_MIN_SPI_ID];
  
OsEE_aarch64_hnd_type
  osEE_aarch64_spi_isr_vectors[OSEE_GIC_ISR_NUM - OSEE_GIC_MIN_SPI_ID];

void osEE_aarch64_isr_wrapper(OsEE_ISR_CTX * p_isr_ctx)
{
  uint32_t irqn;

  while ( 1 ) {
    irqn = osEE_gicc_read_ack();
    if (irqn == OSEE_GIC_SPURIOUS_ISR) {
      break;
    }
    {
#if (!defined(OSEE_SINGLECORE))
      /* Special IIRQ that ask the core to preempt actual TASK */
      if (irqn == OSEE_AARCH64_RESCHEDULE_IIRQ)
      {
        /* TODO: Implement Schedule Point in ISR2 */
      } else
#endif /* !OSEE_SINGLECORE */
      {
        OsEE_aarch64_hnd_type * p_hnd;

        if (irqn < OSEE_GIC_MIN_SPI_ID) {
          CoreIdType const cpu_id = osEE_get_curr_core_id();
          p_hnd = &osEE_aarch64_ppi_isr_vectors[cpu_id][irqn];
        } else {
          p_hnd = &osEE_aarch64_spi_isr_vectors[irqn - OSEE_GIC_MIN_SPI_ID];
        }

        if (p_hnd->cat == OSEE_ISR_CAT_2) {
          TaskType const tid = p_hnd->hnd.tid;
          if (tid != INVALID_TASK) {
            osEE_activate_isr2(tid);
          } else {
            osEE_gicc_eoi(irqn);
          }
        } else if (p_hnd->cat == OSEE_ISR_CAT_1) {
          void (* const p_hnd_func) (void) = p_hnd->hnd.p_hnd_func;
          if (p_hnd_func != NULL) {
            p_hnd_func();
          }
          osEE_gicc_eoi(irqn);
        } else {
          osEE_gicc_eoi(irqn);
        }
      }
    }
  }
}

static void osEE_aarch64_configure_isr2(OsEE_TDB * p_tdb, ISRSource source_id)
{
  /* Pointer to the ISR handler struct */
  OsEE_aarch64_hnd_type * p_hnd;
  /* HW priority mask */
/* IRQ priority handling in jailhouse is bugged */
#if (!defined(OSEE_PLATFORM_JAILHOUSE))
  OsEE_isr_prio const hw_prio_mask =
    osEE_isr2_virt_to_hw_prio(p_tdb->ready_prio);
#endif /* !OSEE_PLATFORM_JAILHOUSE */

  if (source_id < OSEE_GIC_MIN_SPI_ID) {
    p_hnd = &osEE_aarch64_ppi_isr_vectors[osEE_get_curr_core_id()][source_id];
  } else {
    p_hnd = &osEE_aarch64_spi_isr_vectors[source_id - OSEE_GIC_MIN_SPI_ID];
    /* If the source_id is a SPI. Set Current CPU as target for the given
       source */
#if (!defined(OSEE_PLATFORM_JAILHOUSE))
    osEE_gic_v2_set_itargetsr(source_id, osEE_gic_v2_get_cpuif_mask());
#endif /* !OSEE_PLATFORM_JAILHOUSE */
  }

  /* Configure the handler struct */
  p_hnd->cat     = OSEE_ISR_CAT_2;
  p_hnd->hnd.tid = p_tdb->tid;
#if (!defined(OSEE_PLATFORM_JAILHOUSE))
  osEE_gic_v2_set_hw_prio(source_id, hw_prio_mask);
#endif /* !OSEE_PLATFORM_JAILHOUSE */
  osEE_gic_v2_enable_irq(source_id);
}

FUNC(OsEE_bool, OS_CODE) osEE_cpu_startos(void)
{
  OsEE_bool const continue_startos = osEE_std_cpu_startos();

#if (!defined(OSEE_API_DYNAMIC))
  if (continue_startos == OSEE_TRUE) {
    size_t i;
#if (!defined(OSEE_SINGLECORE))
    CoreIdType const core_id = osEE_get_curr_core_id();
#endif /* !OSEE_SINGLECORE */
/* Initialize ISRs of this core */
    OsEE_KDB * const p_kdb   = osEE_get_kernel();
  
    for (i = 0U; i < (p_kdb->tdb_array_size - 1U); ++i)
    {
      /* ISR2 initialization */
      OsEE_TDB  * const p_tdb = (*p_kdb->p_tdb_ptr_array)[i];

#if (!defined(OSEE_SINGLECORE))
      if (p_tdb->orig_core_id != core_id) {
        continue;
      } else
#endif /* !OSEE_SINGLECORE */
      if (p_tdb->task_type == OSEE_TASK_TYPE_ISR2) {
        osEE_aarch64_configure_isr2(p_tdb, p_tdb->hdb.isr2_src);
      }
    }
#if (defined(OSTICKDURATION))
    osEE_aarch64_system_timer_init();
#endif /* OSTICKDURATION */
  }
#endif /* !OSEE_API_DYNAMIC */

  return continue_startos;
}

#if (defined(OSEE_API_DYNAMIC))
StatusType osEE_hal_set_isr2_source
(
  P2VAR(OsEE_TDB, AUTOMATIC, OS_APPL_DATA)  p_tdb,
  VAR(ISRSource,  AUTOMATIC)                source_id
)
{
  p_tdb->hdb.isr2_src = source_id;

  osEE_aarch64_configure_isr2(p_tdb, source_id);

  return E_OK;
}
#endif /* OSEE_API_DYNAMIC */
