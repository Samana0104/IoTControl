# AGENTS.md

# 3차 미니프로젝트 - Coding Convention

본 문서는 프로젝트에서 AI Agent 및 모든 개발자가 준수해야 하는 코딩 규칙을 정의한다.

AI Agent는 코드 생성, 수정, 리팩터링 시 본 문서의 규칙을 최우선으로 적용한다. 기존 코드와 충돌하는 경우 팀 컨벤션을 우선하며, 임의로 네이밍 규칙을 변경하지 않는다.

---

## 1. Naming Convention

### 1.1 열거형 (Enum)

- 열거형 타입 이름은 PascalCase를 사용한다.
- 내부 열거형 상수는 모두 대문자로 작성한다.
- 여러 단어는 언더바(`_`)로 구분한다.
- typedef를 사용한다.

```c
typedef enum
{
    ON = 1,
    OFF
} LedState;
```

### 1.2 구조체 (Struct)

- 구조체 선언 시 typedef를 사용한다.
- 구조체 태그 이름 앞에 언더바(`_`)를 붙인다.
- typedef 별칭에서는 앞의 언더바를 제거한다.
- 이름은 PascalCase를 사용한다.

```c
typedef struct _ButtonState
{
    Button btn;
    State state;
} ButtonState;
```

### 1.3 함수 (Function)

- 함수 이름은 PascalCase를 사용한다.
- 가능하면 동사로 시작한다.
- 함수가 수행하는 역할을 이름에서 파악할 수 있도록 작성한다.

```c
void PrintState(void)
{
}

void InitServer(void)
{
}

void SendData(void)
{
}

void ReadSensor(void)
{
}
```

### 1.4 변수 (Variable)

- 변수 이름은 camelCase를 사용한다.
- 첫 문자는 소문자, 이후 단어의 첫 문자는 대문자로 작성한다.

```c
int xPos;
int yPos;

int clientCount;
uint8_t sensorState;
```

### 1.5 매크로 및 상수 (Macro / Constant)

- 모든 문자는 대문자로 작성한다.
- 단어 사이에는 언더바(`_`)를 사용한다.
- 매크로뿐 아니라 이름이 있는 상수에도 동일한 규칙을 적용한다.

```c
#define MAX_BUFFER 256
#define BUTTON_COUNT 3
#define HEADER_SIZE 20

const int MAX_RETRY_COUNT = 5;
```

---

## 2. 주의 사항 (IMPORTANT)

### 2.1 Arduino / STM32 동적 메모리 할당 금지

Arduino 및 STM32 펌웨어에서는 동적 메모리 할당을 사용하지 않는다.

**사용 금지 함수 및 연산자**

- malloc()
- calloc()
- realloc()
- free()
- C++ new / delete

금지 예시:

```c
int *arr = (int *)malloc(sizeof(int) * 4);

free(arr);
```

허용 예시:

```c
#define ARRAY_SIZE 4

int arr[ARRAY_SIZE] = {0};
```

- 필요한 메모리는 정적 크기의 배열 또는 사전에 정의한 버퍼로 관리한다.
- AI Agent는 편의성을 이유로 동적 할당 코드를 생성해서는 안 된다.
- 본 제한은 Arduino 및 STM32 대상 코드에 적용한다.

### 2.2 헤더 파일(.h)에 static 사용 금지

헤더 파일에는 `static` 키워드가 포함된 변수 및 함수를 선언하거나 정의하지 않는다.

다음 코드 작성 금지:

```c
// example.h

static int clientCount = 0;

static void PrintState(void);

static inline int GetState(void)
{
    return 1;
}
```

권장 방식:

헤더 파일에는 외부 공개 선언만 작성한다.

```c
// example.h

#ifndef EXAMPLE_H
#define EXAMPLE_H

extern int clientCount;

void PrintState(void);

#endif
```

실제 정의와 구현은 소스 파일에서 진행한다.

```c
// example.c

#include "example.h"

int clientCount = 0;

void PrintState(void)
{
}
```

**주의:** `static` 자체를 프로젝트 전체에서 금지하는 것은 아니다. `.c` 내부에서 파일 범위 제한이나 상태 유지를 위해 사용하는 것은 허용한다.

---

## 3. AI Agent 필수 준수 사항

코드 생성 및 수정 작업을 수행할 때 다음 사항을 준수한다.

1. 기존 프로젝트의 아키텍처와 코딩 컨벤션을 유지한다.
2. 모든 신규 변수, 함수, 구조체 및 열거형에 Naming Convention을 적용한다.
3. Arduino 및 STM32 대상 코드에 동적 메모리 할당을 추가하지 않는다.
4. 헤더 파일에 `static` 키워드를 추가하지 않는다.
5. 기존 함수명이나 자료형을 변경해야 한다면 관련 호출부와 선언부도 일관되게 수정한다.
6. 불필요한 코드 변경 및 요청하지 않은 리팩터링은 지양한다.
7. 프로젝트의 기존 통신 프로토콜과 데이터 구조를 임의로 변경하지 않는다.
8. 규칙을 준수하기 어려운 경우, 임의로 예외를 적용하지 말고 그 사유를 먼저 명시한다.

**본 문서의 규칙은 신규 코드뿐 아니라 AI Agent가 수정하는 모든 코드에 적용한다.**