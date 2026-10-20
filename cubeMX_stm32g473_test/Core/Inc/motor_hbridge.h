#ifndef MOTOR_HBRIDGE_H
#define MOTOR_HBRIDGE_H
#include "stm32g4xx_hal.h"
#include "packet.h"
#include <stdbool.h>
/** @brief 出力ゼロ・再開待ちで初期化し停止理由を復元。
 * @param htim TIM8ハンドル。@return なし。 */
void HBridge_Init(TIM_HandleTypeDef *htim);
/** @brief 5 ms周期の割り込みから出力と期限を更新。@return なし。 */
void HBridge_Update(void);
/** @brief コマンドを割り込み排他で実行し実出力へ反映。
 * @param request 検証済みパケット。@param response 応答先。@return 対象ならtrue。 */
bool HBridge_Process(const Packet_t *request, Packet_t *response);
/** @brief 即時出力停止し電源断まで保持。初期化前も呼出し可。@return なし。 */
void HBridge_EmergencyStop(void);
#endif
