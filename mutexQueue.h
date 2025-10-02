/*
 * mutexQueue.h
 *
 * Mutex + Condition Variable 기반 고정 크기 원형 큐
 *  - 멀티스레드 안전, 포인터 기반 FIFO
 *  - 생산자/소비자 수 제한 없음(보통 1:1로 사용)
 *
 * 사용 패턴(권장):
 *  - 메인(이벤트 루프) → 워커: pushMutexQueueNoWait()  // 콜백에서 블록 방지
 *  - 워커 → 메인:       pushMutexQueueWait()     // 역압 시 대기 허용
 *  - 워커 입력:         popMutexQueueWaitTimeout()
 *
 * 주의:
 *  - 큐 내부는 포인터만 저장하므로, 포인터가 가리키는 메모리의
 *    할당/해제는 호출자 책임입니다.
 */

#ifndef MUTEX_QUEUE_H
#define MUTEX_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

/* 
 * MQ: Mutex-based fixed-size ring queue
 * 필드 설명:
 *  - uniMutex      : 내부 보호용 뮤텍스
 *  - uniNotEmpty   : 데이터가 생김을 알리는 cond (소비자 대기용)
 *  - uniNotFull    : 공간이 생김을 알리는 cond (생산자 대기용)
 *  - ppvBuffer     : 포인터 슬롯 배열
 *  - ulMaxSize     : 용량
 *  - ulHead/ulTail : 원형 버퍼 인덱스
 *  - ulCnt         : 현재 요소 개수
 */
typedef struct {
    pthread_mutex_t uniMutex;
    pthread_cond_t  uniNotEmpty;
    pthread_cond_t  uniNotFull;
    void**          ppvBuffer;
    size_t          ulMaxSize;
    size_t          ulHead;
    size_t          ulTail;
    size_t          ulCnt;
} MUTEX_QUEUE;

/**
 * @brief 고정 크기 원형 큐 생성
 * @param ulMaxSize 큐 용량(슬롯 수)
 * @return MUTEX_QUEUE* (성공) / NULL (실패)
 */
MUTEX_QUEUE* newMutexQueue(size_t ulMaxSize);

/**
 * @brief 큐 파기 (내부 리소스 해제)
 * @param pstMutexQueue MUTEX_QUEUE*
 * @note 큐 안에 남아있는 포인터의 메모리 해제는 호출자 책임
 */
void freeMutexQueue(MUTEX_QUEUE* pstMutexQueue);

/**
 * @brief Non-blocking push (가득 차 있으면 0x00)
 * @param pstMutexQueue MUTEX_QUEUE*
 * @param pvData 삽입할 포인터
 * @return 0x01(성공) / 0x00(가득 참)
 * @note 이벤트 콜백에서 블로킹을 피하려면 이 API를 사용하세요.
 */
char pushMutexQueueNoWait(MUTEX_QUEUE* pstMutexQueue, void* pvData);

/**
 * @brief Blocking push (가득 차면 공간 생길 때까지 대기)
 * @param pstMutexQueue MUTEX_QUEUE*
 * @param pvData 삽입할 포인터
 * @note 역압을 전달하려면 생산자를 대기시키는 이 API가 유용합니다.
 */
void pushMutexQueueWait(MUTEX_QUEUE* pstMutexQueue, void* pvData);

/**
 * @brief Timeout 지원 pop
 * @param pstMutexQueue MUTEX_QUEUE*
 * @param iTimeoutMsec -1: 무한대기, 0: 즉시 반환, N: 최대 N ms 대기
 * @return 꺼낸 포인터 / 없으면 NULL
 */
void* popMutexQueueWaitTimeout(MUTEX_QUEUE* pstMutexQueue, int iTimeoutMsec);

#endif /* MUTEX_QUEUE_H */
