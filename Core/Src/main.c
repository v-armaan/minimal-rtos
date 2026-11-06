/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f446xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_it.h"


#define MAX_TASKS 3
#define STACK_SIZE 256


/* USER CODE BEGIN PD */
volatile uint32_t system_ticks=0;
volatile uint8_t scheduler_started = 0;

typedef enum{
  UNUSED,
  READY,
  RUNNING,
  BLOCKED
}state;

typedef struct{
  uint32_t *stack_pointer;
  uint8_t state;
  uint32_t delay_ticks;
}TCB;
_Static_assert(sizeof(TCB) == 12, "Unexpected TCB size");

TCB task[MAX_TASKS];
uint32_t current_Task=0;
uint8_t saved_tasks=0;
uint32_t task_stacks[MAX_TASKS][STACK_SIZE] __attribute__ ((aligned(8)));//each stack is 256*4=1KB with 8 byte alignment


void SystemClock_Config(void);
static void MX_GPIO_Init(void);


void SysTick_Init(void){
  SysTick->CTRL=0;//disabling systick for configuration
  SysTick->LOAD=(SystemCoreClock/1000U)-1u;///setting reload register for 1ms
  SysTick->VAL=0;
  NVIC_SetPriority(SysTick_IRQn,14);
  SysTick->CTRL|=(1u<<0|1u<<1|1u<<2);//enabling systick,exception request,source as processor clock


}

void PendSV_Init(void){
  NVIC_SetPriority(PendSV_IRQn,15);
}


void SysTick_Handler(void)
{
    HAL_IncTick();
    system_ticks++;

    if (scheduler_started)
    {
        SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
    }
}
void schedule_next(void){
  uint8_t next_Task;
  if(task[current_Task].state==RUNNING){//ASSIGNING READY TO RUNNING TASK IF IT HASNT BEEN BLOCKED
    task[current_Task].state=READY;}

    for(uint8_t i=1;i<=MAX_TASKS;i++){
      next_Task=(current_Task+i)%MAX_TASKS;//to wrap around the tasks array
      if(task[next_Task].state== READY){
        current_Task=next_Task;
        task[current_Task].state=RUNNING;//marking the selected task as running
        break;
      }
    }
}

__attribute__((naked)) void PendSV_Handler(void){

  __asm volatile(
    
    //CONTEXT SWITCHING ASSEMBLY CODE
    //STORING REGISTERS IN STACK
    "MRS R0,PSP                    \n"//loading current psp into r0
    "STMDB R0!,{R4-R11}            \n"//storing r4-r11
    "LDR R1,=current_Task          \n"//load address of current task
    "LDR R2,[R1]                   \n"//r2 holds the value of current task
    "MOVS R3,#12                   \n"//for offset calculation,each tcb occupies 12 bytes
    "MUL R2,R2,R3                 \n"//offset of sp in tcb,r2 stores the offset now
    "LDR R1,=task                  \n"//R1 HOLDS THE ADDRESS OF TCB
    "STR R0,[R1,R2]                \n"//psp stored in the sp
    //preserving EXC_RETURN,0xFFFFFFFD in the lr after pendsv interrupt
    "PUSH {R3,LR}                  \n"//r3 is just for 8 byte alignment
    "BL schedule_next              \n"//updating current_Task variable
    "POP {R3,LR}                   \n"//increment MSP by 8 bytes

    //LOADING VALUES FROM STACK TO REGISTER
    "LDR R1,=current_Task   \n"
    "LDR R0,[R1]            \n"//r0 HOLDS VALUE OF CURRENT_TASK
    "LDR R1,=task           \n"//r1 holds address of tcb
    "MOVS R3,#12            \n"//for offset calculation
    "MUL R2,R0,R3          \n"//offset calculation
    "ADDS R2,R2,R1          \n"
    "LDR R3,[R2]            \n"
    "MSR psp,R3             \n"//sp loaded in psp register
    "MRS R0,psp             \n"//storing psp in r0 for operations
    "LDMIA R0!,{R4-R11}     \n"//loaded r4-r11 from stack
    "MSR PSP,R0             \n"//psp now stores to hardware saved exception frame
    "BX LR                  \n"//automatically restores the other registers


  );
  
}
/*---------------------------------------------------*/
/*--------------------------------------------------*/



void idle_task(void){
  while(1){}
}
void task_exit(void){//function to deal with tasks returning unexpectedly
  while(1){}

}
/*----------------------------*/
/*FUNCTION TO INITIALIZE TASKS*/
/*----------------------------*/
void task_create(void (*task_func)(void),uint8_t task_id){
  if(task_id<MAX_TASKS){
  uint32_t *sp=&task_stacks[task_id][STACK_SIZE];
  *(--sp)=0x01000000;//xpsr
  *(--sp)=(uint32_t)task_func;//pc,task_func is already a pointer to a function
  *(--sp)=(uint32_t)task_exit;//lr
  *(--sp)=0;//r12
  *(--sp)=0;//r3
  *(--sp)=0;//r2
  *(--sp)=0;//r1
  *(--sp)=0;//r0
  *(--sp)=0;//r11
  *(--sp)=0;//r10
  *(--sp)=0;//r9
  *(--sp)=0;//r8
  *(--sp)=0;//r7
  *(--sp)=0;//r6
  *(--sp)=0;//r5
  *(--sp)=0;//r4
  /*TCB*/
  task[task_id].stack_pointer=sp;
  task[task_id].state=READY;
  task[task_id].delay_ticks=0;
}
}
volatile uint32_t counter0;
void task0(void){

  while(1){
    counter0++;
  }
}
volatile uint32_t counter1;
void task1(void){
  while(1){
    counter1++;
  }
}

uint32_t *task0_stackpointer;
__attribute__((naked)) void SVC_Handler(void)
{
  __asm volatile(
    "LDR R0, =scheduler_started \n"//setting scheduler started to 1
    "MOVS R1, #1                \n"
    "STRB R1, [R0]              \n"

    "LDR R0,=task0_stackpointer\n"//loading address of sp of task0
    "LDR R1,[R0]               \n"//loading sp of task0
    "LDMIA R1!,{R4-R11}        \n"//loading r4-r11 from task stack to registers
    "MSR psp,R1                \n"//loading updated stack pointer into psp
    "LDR LR,=0xfffffffd        \n"//EXC_RETURN
    "BX LR                     \n"

  );
}



/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();


  task_create(task0,0);
  task_create(task1,1);
  current_Task = 0;
  task[0].state = RUNNING;
  task0_stackpointer=task[0].stack_pointer;



  PendSV_Init();
  SysTick_Init();

 __asm volatile("SVC 0\n");/*TO SWITCH TO HANDLER MODE FROM THREAD MODE*/




  while (1)
  {

  }

}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pins : USART_TX_Pin USART_RX_Pin */
  GPIO_InitStruct.Pin = USART_TX_Pin|USART_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
