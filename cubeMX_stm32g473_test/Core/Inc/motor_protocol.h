#ifndef MOTOR_PROTOCOL_H
#define MOTOR_PROTOCOL_H
#include "motor_safety.h"
#include "packet.h"
/** @brief モーター・状態コマンドを検証して処理する。
 * @param motor 安全状態。@param request 検証済みパケット。@param response 応答先。
 * @param now_ms 現在時刻。@return モーター系コマンドならtrue。未知コマンドはfalse。 */
bool MotorProtocol_Handle(MotorSafety *motor, const Packet_t *request, Packet_t *response, uint32_t now_ms);
#endif
