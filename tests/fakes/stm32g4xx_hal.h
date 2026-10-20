#ifndef TEST_HAL_H
#define TEST_HAL_H
#include <stdint.h>
typedef struct { uint32_t unused; } I2C_HandleTypeDef;
uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
#endif
