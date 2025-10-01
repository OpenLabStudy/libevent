/*
 * uart_uds_mutexq.c
 *
 * 목적(아키텍처 개요)
 *  - 메인 스레드(리브벤트 이벤트 루프): UART 수신/프레이밍, UDS 송신, 역압(backpressure) 제어
 *  - 워커 스레드(1개): 연산(Compute) 전담. 메인→워커: in_queue, 워커→메인: out_queue 사용
 *  - 큐: C11 atomics 없이 pthread_mutex + pthread_cond로 동기화된 고정길이 원형 큐(Mutex Queue)
 *  - 워커→메인 알림: eventfd를 통해 메인 이벤트 루프를 깨움(EV_READ)
 *
 * 핵심 포인트
 *  - 메인 콜백(on_uart_read, on_worker_notify)은 "짧게" 유지 (프레임 파싱/큐 입출력/UDS 버퍼링만)
 *  - 워커가 out_queue에 push 후 eventfd로 알림 → 메인에서 out_queue를 가능한 만큼 비움
 *  - UDS write 버퍼 길이가 워터마크를 넘으면 UART EV_READ를 잠시 멈춰 역압을 전달
 *
 * 빌드:
 *   gcc -O2 -Wall -Wextra -pthread -o uart_uds_mutexq uart_uds_mutexq.c -levent
 *
 * 실행:
 *   ./uart_uds_mutexq /dev/ttyS0 /tmp/myapp.sock
 *
 * 테스트 팁(가상 UART):
 *   socat -d -d pty,raw,echo=0 pty,raw,echo=0
 *   출력된 두 포트 중 하나는 송신 스크립트, 하나는 본 프로그램의 UART로 지정
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/util.h>

// ----------------------------- 설정 상수 -----------------------------
#define UART_BAUD         B115200          // UART 속도
#define IN_Q_CAP          256              // 메인→워커 큐 용량
#define OUT_Q_CAP         256              // 워커→메인 큐 용량
#define UDS_HIGH_WM       (256 * 1024)     // UDS write 워터마크(역압 트리거)
#define PARSE_MAX_FRAME   4096             // 프레임 최대 크기(보안/안전상 상한)
#define MAGIC0 0x55                         // 데모 프레임 헤더 식별자 1
#define MAGIC1 0xAA                         // 데모 프레임 헤더 식별자 2

// --------------------------- 메시지 구조체 ---------------------------
/*
 * FrameMsg
 *  - UART에서 파싱된 "입력 프레임"을 표현
 *  - 메인 스레드가 생성하여 in_queue로 전달 → 워커가 소비
 * 필드:
 *  - seq:     수신 시퀀스 (메인에서 증가)
 *  - ts_ns:   수신 타임스탬프(ns)
 *  - type:    프레임 타입(프로토콜 정의에 따름)
 *  - len:     payload 길이
 *  - payload: payload 데이터(동적 할당; 실전은 풀(pool) 권장)
 */
typedef struct {
    uint32_t seq;
    uint64_t ts_ns;
    uint8_t  type;
    size_t   len;
    uint8_t *payload;
} FrameMsg;

/*
 * ResultMsg
 *  - 워커 연산 결과(출력 메시지)를 표현
 *  - 워커가 생성하여 out_queue로 전달 → 메인에서 소비(UDS 송신)
 * 필드:
 *  - seq_in:   입력 프레임의 시퀀스 번호(추적용)
 *  - ts_ns_in: 입력 프레임의 타임스탬프(추적/지연 측정용)
 *  - len:      결과 payload 길이
 *  - payload:  결과 payload(동적 할당; 실전은 풀(pool) 권장)
 */
typedef struct {
    uint32_t seq_in;
    uint64_t ts_ns_in;
    size_t   len;
    uint8_t *payload;
} ResultMsg;

// ------------------------ 유틸(시간/CRC) ----------------------------
/*
 * now_ns
 *  - 현재 시각을 나노초 단위로 반환
 *  - 타임스탬프 기록/지연 측정 등에 사용
 */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

/*
 * crc8_xor (데모용)
 *  - 아주 단순한 XOR 기반 "유사 CRC"
 *  - 실제 제품에서는 CRC-16/32 등 표준 체크섬으로 교체 필요
 */
static uint8_t crc8_xor(const uint8_t* p, size_t n) {
    uint8_t c = 0; for (size_t i=0;i<n;++i) c ^= p[i]; return c;
}

// --------------------------- 뮤텍스 큐 ------------------------------
/*
 * MQ: Mutex-based fixed-size ring queue (멀티스레드 안전)
 *  - pthread_mutex + pthread_cond 기반
 *  - 생산자/소비자 스레드 간 안전한 포인터 전달
 *
 * 필드:
 *  - m:         큐 보호용 뮤텍스
 *  - not_empty: 큐에 데이터가 생겼음을 알리는 조건변수 (소비자 대기용)
 *  - not_full:  큐에 공간이 생겼음을 알리는 조건변수 (생산자 대기용)
 *  - buf:       void* 슬롯 배열(포인터 전달)
 *  - cap:       용량
 *  - head:      다음에 pop할 위치
 *  - tail:      다음에 push할 위치
 *  - cnt:       현재 저장된 요소 수
 *
 * 정책:
 *  - 메인→워커(in_queue): 메인은 절대 블록되면 안 됨 → push_nowait
 *  - 워커→메인(out_queue): 워커는 공간 날 때까지 대기 허용 → push_wait
 */
typedef struct {
    pthread_mutex_t uniMutex;
    pthread_cond_t  uniNotEmpty;
    pthread_cond_t  uniNotFull;
    void **ppvBuffer;
    size_t ulCap, ulHead, ulTail, ulCnt;
} MUTEX_QUEUE;

/*
 * newMutexQueue
 *  - 고정 크기 원형 큐 할당 및 초기화
 *  - 반환: MUTEX_QUEUE* (실패 시 NULL)
 */
static MUTEX_QUEUE* newMutexQueue(size_t ulCap) {
    MUTEX_QUEUE* pstMutexQueue = (MUTEX_QUEUE*)calloc(1, sizeof(MUTEX_QUEUE));
    if (!pstMutexQueue) 
        return NULL;

    pstMutexQueue->ppvBuffer = (void**)calloc(ulCap, sizeof(void*));
    if (!pstMutexQueue->ppvBuffer) { 
        free(pstMutexQueue); 
        return NULL; 
    }

    pstMutexQueue->ulCap = ulCap;
    pthread_mutex_init(&pstMutexQueue->uniMutex, NULL);
    pthread_cond_init(&pstMutexQueue->uniNotEmpty, NULL);
    pthread_cond_init(&pstMutexQueue->uniNotFull, NULL);
    return pstMutexQueue;
}

/*
 * freeMutexQueue
 *  - 큐의 내부 리소스 해제
 *  - 주의: 큐 안에 남아있는 포인터 자체의 free는 호출자 책임
 */
static void freeMutexQueue(MUTEX_QUEUE* pstMutexQueue) {
    if (!pstMutexQueue) 
        return;

    pthread_mutex_destroy(&pstMutexQueue->uniMutex);
    pthread_cond_destroy(&pstMutexQueue->uniNotEmpty);
    pthread_cond_destroy(&pstMutexQueue->uniNotFull);
    free(pstMutexQueue->ppvBuffer);
    free(pstMutexQueue);
}

/*
 * pushMutexQueueNoWait
 *  - Non-blocking push (메인 루프 전용)
 *  - 가득 차 있으면 false 반환(드롭 정책 적용)
 */
static char pushMutexQueueNoWait(MUTEX_QUEUE* pstMutexQueue, void* pvData) {
    bool ok = true;
    pthread_mutex_lock(&pstMutexQueue->uniMutex);
    if (pstMutexQueue->ulCnt == pstMutexQueue->ulCap) { // full
        ok = false;
    } else {
        pstMutexQueue->ppvBuffer[pstMutexQueue->ulTail] = pvData;
        pstMutexQueue->ulTail = (pstMutexQueue->ulTail + 1) % pstMutexQueue->ulCap;
        pstMutexQueue->ulCnt++;
        pthread_cond_signal(&pstMutexQueue->uniNotEmpty);
    }
    pthread_mutex_unlock(&pstMutexQueue->uniMutex);
    return ok;
}

/*
 * mq_push_wait
 *  - Blocking push (워커 전용)
 *  - 공간이 생길 때까지 not_full 조건변수 대기
 *  - 역압(backpressure) 전달 목적: 메인이 느리면 워커가 자연스럽게 대기
 */
static void mq_push_wait(MUTEX_QUEUE* pstMutexQueue, void* pvData) {
    pthread_mutex_lock(&pstMutexQueue->uniMutex);

    while (pstMutexQueue->ulCnt == pstMutexQueue->ulCap) 
        pthread_cond_wait(&pstMutexQueue->uniNotFull, &pstMutexQueue->uniMutex);

    pstMutexQueue->ppvBuffer[pstMutexQueue->ulTail] = pvData;
    pstMutexQueue->ulTail = ((pstMutexQueue->ulTail + 1) % pstMutexQueue->ulCap);
    pstMutexQueue->ulCnt++;
    pthread_cond_signal(&pstMutexQueue->uniNotEmpty);
    pthread_mutex_unlock(&pstMutexQueue->uniMutex);
}

/*
 * mq_pop_wait_timeout
 *  - Timeout 지원 pop (워커가 in_queue에서 사용)
 *  - timeout_ms: -1(무한대기), 0(즉시 반환), N(ms)
 *  - 반환: 요소 포인터(없으면 NULL)
 */
static void* mq_pop_wait_timeout(MUTEX_QUEUE* pstMutexQueue, int timeout_ms) {
    void* ptr = NULL;
    pthread_mutex_lock(&pstMutexQueue->m);

    if (timeout_ms < 0) {
        while (pstMutexQueue->cnt == 0) 
            pthread_cond_wait(&pstMutexQueue->not_empty, &pstMutexQueue->m);
    } else if (timeout_ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        while (pstMutexQueue->cnt == 0) {
            int rc = pthread_cond_timedwait(&pstMutexQueue->not_empty, &pstMutexQueue->m, &ts);
            if (rc == ETIMEDOUT) break;
        }
    } // else 0ms: fall-through (즉시 확인)

    if (pstMutexQueue->cnt > 0) {
        ptr = pstMutexQueue->buf[pstMutexQueue->head];
        pstMutexQueue->head = (q->head + 1) % q->cap;
        q->cnt--;
        pthread_cond_signal(&q->not_full);
    }
    pthread_mutex_unlock(&q->m);
    return ptr;
}

// --------------------------- 앱 컨텍스트 ----------------------------
/*
 * AppCtx
 *  - 애플리케이션 전역 상태를 보관하는 컨텍스트
 * 필드:
 *  - base:                libevent event_base
 *  - uart_fd:             UART 파일 디스크립터
 *  - ev_uart_read:        UART EV_READ 이벤트
 *  - rxbuf:               수신 누적 버퍼(evbuffer). 파서가 여기서 프레임 추출
 *  - uds_path:            UDS 소켓 경로
 *  - bev_uds:             UDS bufferevent(클라이언트)
 *  - ev_reconnect_timer:  UDS 재연결 타이머 이벤트
 *  - efd_worker_notify:   워커→메인 알림용 eventfd (EV_READ로 등록)
 *  - ev_worker_notify:    eventfd용 이벤트 객체
 *  - in_q, out_q:         뮤텍스 큐 (메인→워커, 워커→메인)
 *  - worker_tid:          워커 스레드 핸들
 *  - worker_stop:         워커 종료 플래그
 *  - uart_paused:         현재 UART EV_READ가 일시 중단 상태인지
 *  - seq_rx:              수신 시퀀스 번호(프레임 식별/추적용)
 */
typedef struct {
    struct event_base* base;

    // UART
    int uart_fd;
    struct event* ev_uart_read;
    struct evbuffer* rxbuf;

    // UDS client
    char uds_path[256];
    struct bufferevent* bev_uds;
    struct event* ev_reconnect_timer;

    // worker notify
    int efd_worker_notify;
    struct event* ev_worker_notify;

    // queues
    MQ* in_q;   // FrameMsg* (메인→워커)
    MQ* out_q;  // ResultMsg* (워커→메인)

    // worker
    pthread_t worker_tid;
    volatile bool worker_stop;

    // backpressure
    bool uart_paused;

    // sequence
    uint32_t seq_rx;
} AppCtx;

static AppCtx* g_app = NULL; // 시그널 핸들러에서 접근용(간단화)

// ------------------------ UART 헬퍼 -------------------------------
/*
 * uart_open_config
 *  - UART 디바이스를 열고 raw/non-blocking/115200bps로 설정
 *  - 성공 시 fd 반환, 실패 시 -1
 */
static int uart_open_config(const char* dev) {
    int fd = open(dev, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) { close(fd); return -1; }

    cfmakeraw(&tio);
    cfsetispeed(&tio, UART_BAUD);
    cfsetospeed(&tio, UART_BAUD);
    tio.c_cflag |= CLOCAL | CREAD;           // 로컬/수신 enable
    tio.c_cflag &= ~PARENB;                  // no parity
    tio.c_cflag &= ~CSTOPB;                  // 1 stop
    tio.c_cflag &= ~CSIZE; tio.c_cflag |= CS8; // 8-bit
    tio.c_cc[VMIN]  = 0;                     // non-blocking read
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) { close(fd); return -1; }
    return fd;
}

/*
 * uart_pause / uart_resume
 *  - UDS 출력 버퍼가 커지면 UART 입력을 잠시 멈춰 상류에 역압 전달
 *  - 워터마크 이하로 떨어지면 다시 재개
 */
static void uart_pause(AppCtx* a) {
    if (a->uart_paused) return;
    if (a->ev_uart_read) event_del(a->ev_uart_read);
    a->uart_paused = true;
}
static void uart_resume(AppCtx* a) {
    if (!a->uart_paused) return;
    if (a->ev_uart_read) event_add(a->ev_uart_read, NULL);
    a->uart_paused = false;
}

// ------------------------ UDS (클라이언트) ------------------------
/*
 * forward 선언: 재연결 타이머에서 호출
 */
static void uds_connect_start(AppCtx* a);

/*
 * on_uds_event
 *  - UDS bufferevent 이벤트 콜백
 *  - 연결됨(BEV_EVENT_CONNECTED): 워터마크 설정
 *  - 끊김/에러: bufferevent 해제 후 재연결 타이머 스케줄
 */
static void on_uds_event(struct bufferevent* bev, short what, void* arg) {
    AppCtx* a = (AppCtx*)arg;
    if (what & BEV_EVENT_CONNECTED) {
        bufferevent_setwatermark(bev, EV_WRITE, 0, UDS_HIGH_WM);
        fprintf(stderr, "[UDS] Connected\n");
        return;
    }
    if (what & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
        int err = EVUTIL_SOCKET_ERROR();
        fprintf(stderr, "[UDS] Disconnected (err=%d %s)\n", err, evutil_socket_error_to_string(err));
        bufferevent_free(a->bev_uds);
        a->bev_uds = NULL;

        // 간단한 고정 지연(실전: 지수 백오프 권장)
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        if (!a->ev_reconnect_timer)
            a->ev_reconnect_timer = evtimer_new(a->base, (void(*)(evutil_socket_t, short, void*))uds_connect_start, a);
        evtimer_add(a->ev_reconnect_timer, &tv);
    }
}

/*
 * uds_connect_start
 *  - UDS 클라이언트 bufferevent를 만들고 지정된 경로로 connect
 *  - 실패 시 bufferevent 해제(타이머에서 재시도)
 */
static void uds_connect_start(AppCtx* a) {
    if (a->bev_uds) return;
    a->bev_uds = bufferevent_socket_new(a->base, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!a->bev_uds) { fprintf(stderr, "[UDS] bev alloc failed\n"); return; }
    bufferevent_setcb(a->bev_uds, NULL, NULL, on_uds_event, a);

    struct sockaddr_un sun; memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    evutil_strlcpy(sun.sun_path, a->uds_path, sizeof(sun.sun_path));

    if (bufferevent_socket_connect(a->bev_uds, (struct sockaddr*)&sun, sizeof(sun)) != 0) {
        fprintf(stderr, "[UDS] connect() immediate fail\n");
        bufferevent_free(a->bev_uds); a->bev_uds = NULL;
    }
}

// ------------------------ 역압(Backpressure) -----------------------
/*
 * maybe_apply_backpressure
 *  - UDS output evbuffer 길이를 확인하여
 *    워터마크 이상이면 UART 입력을 일시 중단, 아니면 재개
 */
static void maybe_apply_backpressure(AppCtx* a) {
    if (!a->bev_uds) return;
    struct evbuffer* out = bufferevent_get_output(a->bev_uds);
    size_t olen = evbuffer_get_length(out);
    if (olen >= UDS_HIGH_WM) uart_pause(a);
    else                     uart_resume(a);
}

// ------------------------ 워커 연산(Compute) -----------------------
/*
 * compute_result (데모 구현)
 *  - 실제 연산(DCM/좌표변환/필터 등)을 넣을 자리
 *  - 여기서는 payload 전체를 XOR 0xFF 하고 앞에 8바이트 헤더("RES\0"+seq)를 붙임
 * 반환:
 *  - ResultMsg* (실패 시 NULL)
 *  - 호출자가 free해야 함
 */
static ResultMsg* compute_result(const FrameMsg* in) {
    ResultMsg* r = (ResultMsg*)calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->seq_in = in->seq;
    r->ts_ns_in = in->ts_ns;
    r->len = in->len + 8;
    r->payload = (uint8_t*)malloc(r->len);
    if (!r->payload) { free(r); return NULL; }

    r->payload[0]='R'; r->payload[1]='E'; r->payload[2]='S'; r->payload[3]=0;
    memcpy(&r->payload[4], &in->seq, 4);
    for (size_t i=0;i<in->len;++i) r->payload[8+i] = (uint8_t)(in->payload[i]^0xFF);
    return r;
}

/*
 * worker_main
 *  - 워커 스레드 엔트리
 *  - in_queue에서 FrameMsg를 timeout 대기하며 pop
 *  - compute_result → out_queue에 push_wait (공간 대기 허용)
 *  - push 후 eventfd로 메인 루프를 깨움
 */
static void* worker_main(void* arg) {
    AppCtx* a = (AppCtx*)arg;
    while (!a->worker_stop) {
        // 입력 대기 (최대 10ms): 종료 플래그 확인을 위해 타임아웃 사용
        FrameMsg* m = (FrameMsg*)mq_pop_wait_timeout(a->in_q, 10);
        if (!m) continue;

        ResultMsg* r = compute_result(m);
        free(m->payload); free(m);

        if (r) {
            // 출력 큐에 공간이 생길 때까지 대기(역압 반영)
            mq_push_wait(a->out_q, r);
            // 메인 이벤트 루프 깨우기 (EV_READ 트리거)
            uint64_t one = 1; (void)write(a->efd_worker_notify, &one, sizeof(one));
        }
    }
    return NULL;
}

// ------------------------ 결과 전송/알림 ---------------------------
/*
 * send_one_result
 *  - ResultMsg를 UDS로 프레이밍하여 전송
 *  - 프레임 포맷(데모): [MAGIC0 MAGIC1][len16 LE][type=0x01][payload][crc8]
 *  - 실패/미연결이면 drop
 */
static void send_one_result(AppCtx* a, ResultMsg* r) {
    if (!a->bev_uds) { free(r->payload); free(r); return; }

    uint16_t len16 = (uint16_t)(1 + r->len); // type(1) + payload
    uint8_t hdr[2+2+1];
    hdr[0]=MAGIC0; hdr[1]=MAGIC1;
    memcpy(&hdr[2], &len16, 2);
    hdr[4]=0x01; // type

    uint8_t crc = hdr[4];
    for (size_t i=0;i<r->len;++i) crc ^= r->payload[i];

    bufferevent_write(a->bev_uds, hdr, sizeof(hdr));
    bufferevent_write(a->bev_uds, r->payload, r->len);
    bufferevent_write(a->bev_uds, &crc, 1);

    free(r->payload); free(r);
}

/*
 * on_worker_notify
 *  - 워커→메인 알림(eventfd) EV_READ 콜백
 *  - eventfd 카운터를 드레인하고 out_queue에서 가능한 만큼 pop해 UDS로 write
 *  - 처리 후 backpressure 적용
 */
static void on_worker_notify(evutil_socket_t fd, short ev, void* arg) {
    (void)ev; AppCtx* a = (AppCtx*)arg;

    // eventfd 카운터 드레인(여러 번 깨어난 경우 합산)
    uint64_t cnt; while (read(fd, &cnt, sizeof(cnt)) > 0) {}

    // out_queue 비우기(배치 처리로 깨어나기 비용 상쇄)
    for (;;) {
        ResultMsg* r = (ResultMsg*)mq_pop_wait_timeout(a->out_q, 0);
        if (!r) break;
        send_one_result(a, r);
    }
    maybe_apply_backpressure(a);
}

// ------------------------ UART 파서/이벤트 -------------------------
/*
 * parse_and_enqueue
 *  - 누적 버퍼(rxbuf)에서 프레임을 추출해 FrameMsg로 만들어 in_queue에 push_nowait
 *  - 데모 프레임: [MAGIC0 MAGIC1][LEN16][TYPE][PAYLOAD...][CRC8]
 *  - CRC/길이 오류 시 재동기화(앞부분 드레인) 후 계속 탐색
 *  - 메인은 블록되면 안 되므로 in_queue 가득 시 "드롭-뉴" 정책 적용
 */
static void parse_and_enqueue(AppCtx* a) {
    while (1) {
        size_t n = evbuffer_get_length(a->rxbuf);
        if (n < 2+2+1+1) return; // 최소 길이 미만

        uint8_t* p = evbuffer_pullup(a->rxbuf, n);
        if (!p) return;

        // MAGIC 탐색(재동기화)
        size_t i=0;
        for (; i+1<n; ++i) { if (p[i]==MAGIC0 && p[i+1]==MAGIC1) break; }
        if (i>0) { evbuffer_drain(a->rxbuf, i); continue; }
        if (n < 2+2+1+1) return;

        uint16_t len16; memcpy(&len16, p+2, 2);
        if (len16 > PARSE_MAX_FRAME) { evbuffer_drain(a->rxbuf, 2); continue; }
        size_t need = 2+2+1 + len16 + 1;
        if (n < need) return; // 불완전

        uint8_t type = p[4];
        const uint8_t* payload = p+5;
        size_t paylen = len16 - 1;
        uint8_t got_crc = p[5+paylen];

        uint8_t c = type; for (size_t k=0;k<paylen;++k) c ^= payload[k];
        if (c != got_crc) { evbuffer_drain(a->rxbuf, 1); continue; }

        // 유효 프레임 → FrameMsg 생성 후 in_queue에 삽입(비블로킹)
        FrameMsg* m = (FrameMsg*)calloc(1, sizeof(*m));
        if (m) {
            m->seq   = ++a->seq_rx;
            m->ts_ns = now_ns();
            m->type  = type;
            m->len   = paylen;
            m->payload = (uint8_t*)malloc(paylen);
            if (m->payload && paylen) memcpy(m->payload, payload, paylen);

            if (!mq_push_nowait(a->in_q, m)) {
                // 혼잡: 드롭-뉴
                free(m->payload); free(m);
            }
        }
        evbuffer_drain(a->rxbuf, need); // 소비
    }
}

/*
 * on_uart_read
 *  - UART EV_READ 콜백
 *  - fd에서 가능한 만큼 읽어 rxbuf에 누적하고, parse_and_enqueue로 프레임 추출
 *  - 읽기 에러(EAGAIN 제외) 시 로그 출력
 */
static void on_uart_read(evutil_socket_t fd, short ev, void* arg) {
    (void)ev; AppCtx* a = (AppCtx*)arg;
    uint8_t buf[4096];

    for (;;) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r > 0) {
            evbuffer_add(a->rxbuf, buf, (size_t)r);
        } else if (r == 0) {
            // UART의 경우 EOF 개념이 모호하므로 EAGAIN처럼 다룸
            break;
        } else {
            if (errno==EAGAIN || errno==EWOULDBLOCK) break;
            perror("uart read");
            break;
        }
    }
    parse_and_enqueue(a);
}

// ------------------------ 시그널/메인 루틴 ------------------------
/*
 * on_sig
 *  - SIGINT/SIGTERM 핸들러
 *  - event_base_loopexit로 메인 루프 종료를 트리거
 */
static void on_sig(evutil_socket_t sig, short ev, void* arg) {
    (void)ev; (void)arg;
    if (!g_app) return;
    fprintf(stderr, "Signal %d => shutdown\n", (int)sig);
    event_base_loopexit(g_app->base, NULL);
}

/*
 * main
 *  - 초기화 → 이벤트 루프 → 종료/정리 순으로 수행
 * 순서:
 *  1) event_base 생성, 시그널 이벤트 등록
 *  2) in/out 큐 생성
 *  3) worker notify용 eventfd 생성 및 EV_READ 이벤트 등록
 *  4) UART open/설정 및 EV_READ 이벤트 등록
 *  5) UDS connect 시작
 *  6) 워커 스레드 생성
 *  7) event_base_dispatch로 루프 진입
 *  8) 종료 시 리소스 정리
 */
int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <UART_DEV> <UDS_PATH>\n", argv[0]);
        return 1;
    }
    const char* uart_dev = argv[1];
    const char* uds_path = argv[2];

    AppCtx app; memset(&app, 0, sizeof(app)); g_app = &app;

    // 1) event_base + 시그널
    app.base = event_base_new();
    if (!app.base) { fprintf(stderr, "event_base_new failed\n"); return 1; }
    struct event* ev_sigint  = evsignal_new(app.base, SIGINT,  on_sig, NULL);
    struct event* ev_sigterm = evsignal_new(app.base, SIGTERM, on_sig, NULL);
    event_add(ev_sigint, NULL); event_add(ev_sigterm, NULL);

    // 2) 큐
    app.in_q  = mq_new(IN_Q_CAP);
    app.out_q = mq_new(OUT_Q_CAP);
    if (!app.in_q || !app.out_q) { fprintf(stderr, "queue alloc failed\n"); return 1; }

    // 3) eventfd + EV_READ (워커→메인 알림)
    app.efd_worker_notify = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (app.efd_worker_notify < 0) { perror("eventfd"); return 1; }
    app.ev_worker_notify = event_new(app.base, app.efd_worker_notify, EV_READ|EV_PERSIST, on_worker_notify, &app);
    event_add(app.ev_worker_notify, NULL);

    // 4) UART open + EV_READ
    app.uart_fd = uart_open_config(uart_dev);
    if (app.uart_fd < 0) { perror("uart_open"); return 1; }
    app.rxbuf = evbuffer_new();
    app.ev_uart_read = event_new(app.base, app.uart_fd, EV_READ|EV_PERSIST, on_uart_read, &app);
    event_add(app.ev_uart_read, NULL);

    // 5) UDS connect
    evutil_strlcpy(app.uds_path, uds_path, sizeof(app.uds_path));
    uds_connect_start(&app);

    // 6) 워커 스레드 시작
    app.worker_stop = false;
    if (pthread_create(&app.worker_tid, NULL, worker_main, &app) != 0) {
        perror("pthread_create"); return 1;
    }

    // 7) 이벤트 루프
    fprintf(stderr, "Running... UART=%s  UDS=%s\n", uart_dev, uds_path);
    event_base_dispatch(app.base);

    // 8) 종료(정리)
    app.worker_stop = true;
    pthread_join(app.worker_tid, NULL);

    if (app.bev_uds) bufferevent_free(app.bev_uds);
    if (app.ev_reconnect_timer) event_free(app.ev_reconnect_timer);
    if (app.ev_uart_read) event_free(app.ev_uart_read);
    if (app.rxbuf) evbuffer_free(app.rxbuf);
    if (app.ev_worker_notify) event_free(app.ev_worker_notify);
    if (app.efd_worker_notify >= 0) close(app.efd_worker_notify);
    if (app.uart_fd >= 0) close(app.uart_fd);
    if (ev_sigint) event_free(ev_sigint);
    if (ev_sigterm) event_free(ev_sigterm);
    if (app.in_q) mq_free(app.in_q);
    if (app.out_q) mq_free(app.out_q);
    if (app.base) event_base_free(app.base);

    fprintf(stderr, "Bye.\n");
    return 0;
}
