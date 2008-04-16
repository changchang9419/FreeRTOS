/*
	FreeRTOS.org V5.0.0 - Copyright (C) 2003-2008 Richard Barry.

	This file is part of the FreeRTOS.org distribution.

	FreeRTOS.org is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	FreeRTOS.org is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FreeRTOS.org; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

	A special exception to the GPL can be applied should you wish to distribute
	a combined work that includes FreeRTOS.org, without being obliged to provide
	the source code for any proprietary components.  See the licensing section 
	of http://www.FreeRTOS.org for full details of how and when the exception
	can be applied.

    ***************************************************************************
    ***************************************************************************
    *                                                                         *
    * SAVE TIME AND MONEY!  We can port FreeRTOS.org to your own hardware,    *
    * and even write all or part of your application on your behalf.          *
    * See http://www.OpenRTOS.com for details of the services we provide to   *
    * expedite your project.                                                  *
    *                                                                         *
    ***************************************************************************
    ***************************************************************************

	Please ensure to read the configuration and relevant port sections of the
	online documentation.

	http://www.FreeRTOS.org - Documentation, latest information, license and 
	contact details.

	http://www.SafeRTOS.com - A version that is certified for use in safety 
	critical systems.

	http://www.OpenRTOS.com - Commercial support, development, porting, 
	licensing and training services.
*/


/*-----------------------------------------------------------
 * Components that can be compiled to either ARM or THUMB mode are
 * contained in port.c  The ISR routines, which can only be compiled
 * to ARM mode, are contained in this file.
 *----------------------------------------------------------*/

/* This file must always be compiled to ARM mode as it contains ISR 
definitions. */
#pragma ARM

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"

/* Constants required to handle interrupts. */
#define portTIMER_MATCH_ISR_BIT		( ( unsigned portCHAR ) 0x01 )
#define portCLEAR_VIC_INTERRUPT		( ( unsigned portLONG ) 0 )

/*-----------------------------------------------------------*/

/* The code generated by the Keil compiler does not maintain separate
stack and frame pointers. The portENTER_CRITICAL macro cannot therefore
use the stack as per other ports.  Instead a variable is used to keep
track of the critical section nesting.  This variable has to be stored
as part of the task context and must be initialised to a non zero value. */

#define portNO_CRITICAL_NESTING		( ( unsigned portLONG ) 0 )
volatile unsigned portLONG ulCriticalNesting = 9999UL;

/*-----------------------------------------------------------*/

/* ISR to handle manual context switches (from a call to taskYIELD()). */
void vPortYieldProcessor( void );

/* 
 * The scheduler can only be started from ARM mode, hence the inclusion of this
 * function here.
 */
void vPortISRStartFirstTask( void );

/*-----------------------------------------------------------*/

void vPortISRStartFirstTask( void )
{
	/* Simply start the scheduler.  This is included here as it can only be
	called from ARM mode. */
	portRESTORE_CONTEXT();
}
/*-----------------------------------------------------------*/

/*
 * Interrupt service routine for the SWI interrupt.  The vector table is
 * configured within startup.s.
 *
 * vPortYieldProcessor() is used to manually force a context switch.  The
 * SWI interrupt is generated by a call to taskYIELD() or portYIELD().
 */
void vPortYieldProcessor( void ) __task
{
	/* Within an IRQ ISR the link register has an offset from the true return 
	address, but an SWI ISR does not.  Add the offset manually so the same 
	ISR return code can be used in both cases. */
	__asm{ ADD	LR, LR, #4 };

	/* Perform the context switch. */
	portSAVE_CONTEXT();
	vTaskSwitchContext();
	portRESTORE_CONTEXT();	
}
/*-----------------------------------------------------------*/

/* 
 * The ISR used for the scheduler tick depends on whether the cooperative or
 * the preemptive scheduler is being used.
 */

#if configUSE_PREEMPTION == 0

	/* 
	 * The cooperative scheduler requires a normal IRQ service routine to 
	 * simply increment the system tick. 
	 */
	void vNonPreemptiveTick( void );
	void vNonPreemptiveTick( void ) __irq
	{
		/* Increment the tick count - this may make a delaying task ready
		to run - but a context switch is not performed. */		
		vTaskIncrementTick();

		/* Ready for the next interrupt. */
		T0IR = portTIMER_MATCH_ISR_BIT;
		VICVectAddr = portCLEAR_VIC_INTERRUPT;
	}

#else

	/* 
	 * The preemptive scheduler ISR is defined as "naked" as the full context 
	 * is saved on entry as part of the context switch. 
	 */
	void vPreemptiveTick( void );
	void vPreemptiveTick( void ) __task
	{
		/* Save the context of the current task. */
		portSAVE_CONTEXT();	

		/* Increment the tick count - this may make a delayed task ready to 
		run. */
		vTaskIncrementTick();

		/* Find the highest priority task that is ready to run. */
		vTaskSwitchContext();

		/* Ready for the next interrupt. */
		T0IR = portTIMER_MATCH_ISR_BIT;
		VICVectAddr = portCLEAR_VIC_INTERRUPT;
		
		/* Restore the context of the highest priority task that is ready to 
		run. */
		portRESTORE_CONTEXT();
	}
#endif
/*-----------------------------------------------------------*/

/*
 * The interrupt management utilities can only be called from ARM mode.  When
 * KEIL_THUMB_INTERWORK is defined the utilities are defined as functions here 
 * to ensure a switch to ARM mode.  When KEIL_THUMB_INTERWORK is not defined 
 * then the utilities are defined as macros in portmacro.h - as per other 
 * ports.
 */
#ifdef KEIL_THUMB_INTERWORK

	void vPortDisableInterruptsFromThumb( void ) __task;
	void vPortEnableInterruptsFromThumb( void ) __task;

	void vPortDisableInterruptsFromThumb( void ) __task
	{
		__asm{ STMDB	SP!, {R0}		};	/* Push R0.									*/
		__asm{ MRS		R0, CPSR		};	/* Get CPSR.								*/
		__asm{ ORR		R0, R0, #0xC0	};	/* Disable IRQ, FIQ.						*/
		__asm{ MSR		CPSR_CXSF, R0	};	/* Write back modified value.				*/
		__asm{ LDMIA	SP!, {R0}		};	/* Pop R0.									*/
		__asm{ BX		R14				};	/* Return back to thumb.					*/
	}
			
	void vPortEnableInterruptsFromThumb( void )	__task
	{
		__asm{ STMDB	SP!, {R0}		};	/* Push R0.									*/
		__asm{ MRS		R0, CPSR		};	/* Get CPSR.								*/
		__asm{ BIC		R0, R0, #0xC0	};	/* Enable IRQ, FIQ.							*/
		__asm{ MSR		CPSR_CXSF, R0	};	/* Write back modified value.				*/
		__asm{ LDMIA	SP!, {R0}		};	/* Pop R0. 									*/
		__asm{ BX		R14				};	/* Return back to thumb.					*/
	}

#endif /* KEIL_THUMB_INTERWORK */



/* The code generated by the Keil compiler does not maintain separate
stack and frame pointers. The portENTER_CRITICAL macro cannot therefore
use the stack as per other ports.  Instead a variable is used to keep
track of the critical section nesting.  This necessitates the use of a 
function in place of the macro. */

void vPortEnterCritical( void )
{
	/* Disable interrupts as per portDISABLE_INTERRUPTS(); 							*/
	__asm{ STMDB	SP!, {R0}		};	/* Push R0.									*/
	__asm{ MRS		R0, CPSR		};	/* Get CPSR.								*/
	__asm{ ORR		R0, R0, #0xC0	};	/* Disable IRQ, FIQ.						*/
	__asm{ MSR		CPSR_CXSF, R0	};	/* Write back modified value.				*/
	__asm{ LDMIA	SP!, {R0}		};	/* Pop R0.									*/

	/* Now interrupts are disabled ulCriticalNesting can be accessed 
	directly.  Increment ulCriticalNesting to keep a count of how many times
	portENTER_CRITICAL() has been called. */
	ulCriticalNesting++;
}

void vPortExitCritical( void )
{
	if( ulCriticalNesting > portNO_CRITICAL_NESTING )
	{
		/* Decrement the nesting count as we are leaving a critical section. */
		ulCriticalNesting--;

		/* If the nesting level has reached zero then interrupts should be
		re-enabled. */
		if( ulCriticalNesting == portNO_CRITICAL_NESTING )
		{
			/* Enable interrupts as per portEXIT_CRITICAL(). */
			__asm{ STMDB	SP!, {R0}		};	/* Push R0.							*/
			__asm{ MRS		R0, CPSR		};	/* Get CPSR.						*/
			__asm{ BIC		R0, R0, #0xC0	};	/* Enable IRQ, FIQ.					*/
			__asm{ MSR		CPSR_CXSF, R0	};	/* Write back modified value.		*/
			__asm{ LDMIA	SP!, {R0}		};	/* Pop R0. 							*/
		}
	}
}










