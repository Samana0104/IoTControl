#pragma once

#include <ADht.h>
#include <AWiFi.h>

// 서버 패킷 송수신 (AWiFi는 바이트 송수신만, 프레임 조립/CRC/분배는 여기서)
//   수신: APacketProcess()가 프레임을 모아 cmd/payload로 떼고 PACKET_TABLE에서 처리 함수를 찾아 호출
//   송신: APacketLogin(), APacketSendDhtAck() 등 Make*Packet으로 만든 프레임 전송

typedef enum
{
    LOGIN_SUCCESS = 0,
    LOGIN_REJECTED,   // 서버가 RESULT_FAIL 응답 (ID/PW 틀림)
    LOGIN_NO_SERVER,  // 서버 미연결 또는 전송 실패
    LOGIN_TIMEOUT,    // ACK_LOGIN 응답 없음
    LOGIN_BAD_PACKET, // ACK_LOGIN payload가 맞지 않음
    LOGIN_NO_MEMBER   // 저장된 계정 없음 (member set 필요)
} LoginResult;

// 송수신에 쓸 WiFi와 패킷 처리에 쓸 장치 연결, setup()에서 한 번
void APacketBegin(AWiFi &wifi, ADht &dht);

// 서버 프레임을 들어온 만큼 모아서, 완성된 프레임을 cmd별 처리 함수로 분배 (loop에서 주기적으로 호출)
void APacketProcess();

// 장치 → 서버 REQ_LOGIN: EEPROM에 저장된 계정(member set)으로 로그인
// 서버 연결 직후 호출 (수신 상태 초기화), ACK_LOGIN까지 기다림 (블로킹), 대기 중 들어온 다른 프레임은 버림
LoginResult APacketLogin();

// 장치 → 서버 ACK_DHT: REQ_DHT 응답
bool APacketSendDhtAck(const DhtAckData &ack);
