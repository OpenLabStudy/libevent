# 기준 시선벡터 `V_E_ref` 유지용 자세교정 (IMU 업데이트 기반) — AZ/EL 산출 절차

본 문서는 **기준 시점에 계산/저장된 ENU 시선벡터** `V_E_ref`를 유지하기 위해,  
**새롭게 수신된 IMU(Roll/Pitch/Yaw)** 로부터 현재 자세의 DCM을 구성하고,  
그 결과를 이용해 **페데스탈 명령각(AZ, EL)** 을 산출하는 절차를 정리한다.

---

## 0. 전제(중요)

### 0.1 좌표계
- **ENU(World) 좌표계 `{E}`** : X=East, Y=North, Z=Up (오른손)
- **Base(Body) 좌표계 `{B}`** : X=전방, Y=우측, Z=위쪽 (오른손)

### 0.2 벡터/행렬 정의
- `V_E_ref` : 기준 시점(타겟 고정)에서 얻은 **ENU 기준 시선벡터(단위벡터)**
- IMU Euler(roll, pitch, yaw) → DCM 구성 시
  - `R_BE` : **Base → ENU** 변환 DCM
  - `R_EB` : **ENU → Base** 변환 DCM  
    \[
      R_{EB} = R_{BE}^T
    \]

### 0.3 목표
- 새 IMU 자세에서, `V_E_ref`를 바라보도록 하는 **페데스탈 명령각(AZ_cmd, EL_cmd)** 계산

---

## 1. 전체 흐름 요약

(기준 시점)
 V_E_ref 저장

(새 IMU 수신 시마다 반복)
 IMU Euler → R_BE 계산
 R_EB = transpose(R_BE)
 V_B_des = R_EB * V_E_ref          // 현재 Base에서의 "원하는" LOS
 (AZ_cmd, EL_cmd) = vectorToAzEl(V_B_des)



---

## 2. Step 1 — 기준 시선벡터 `V_E_ref` 준비(이미 완료된 상태)

기준 시점에 당신이 이미 구현한 방식대로 `V_E_ref`를 구하고 저장한다.

- 페데스탈 AZ/EL → `V_B_ref`
- IMU Euler → `R_BE_ref`
- `V_E_ref = R_BE_ref * V_B_ref`

> 본 문서는 **`V_E_ref`가 준비되어 저장된 상태부터** 시작한다.

---

## 3. Step 2 — 새 IMU(Euler) 수신 → `R_BE` 계산

새로운 Roll/Pitch/Yaw가 들어오면, 동일한 방식으로 **`R_BE`(Base→ENU)** 를 만든다.

### 3.1 자료형

```c
typedef struct {
    double dX;
    double dY;
    double dZ;
} VECTOR3;
```

### 3.2 Euler → DCM (`R_BE`)

> 아래 함수는 **R = Rz(yaw) \* Ry(pitch) \* Rx(roll)** 을 구성하며
>  결과 `R`은 **Base → ENU (R_BE)** 로 사용한다.

```c
static void calcDcmFromEuler(double dRoll, double dPitch, double dYaw,
                             double R[3][3])
{
    double dCosRoll  = cos(dRoll);
    double dSinRoll  = sin(dRoll);
    double dCosPitch = cos(dPitch);
    double dSinPitch = sin(dPitch);
    double dCosYaw   = cos(dYaw);
    double dSinYaw   = sin(dYaw);

    /* R = Rz(yaw) * Ry(pitch) * Rx(roll) */

    R[0][0] =  dCosYaw * dCosPitch;
    R[0][1] =  dCosYaw * dSinPitch * dSinRoll - dSinYaw * dCosRoll;
    R[0][2] =  dCosYaw * dSinPitch * dCosRoll + dSinYaw * dSinRoll;

    R[1][0] =  dSinYaw * dCosPitch;
    R[1][1] =  dSinYaw * dSinPitch * dSinRoll + dCosYaw * dCosRoll;
    R[1][2] =  dSinYaw * dSinPitch * dCosRoll - dCosYaw * dSinRoll;

    R[2][0] = -dSinPitch;
    R[2][1] =  dCosPitch * dSinRoll;
    R[2][2] =  dCosPitch * dCosRoll;
}
```



## 4. Step 3 — `R_BE` 전치 → `R_EB` 생성 (ENU→Base)

새 IMU에서 얻은 `R_BE`를 전치하여 `R_EB`를 만든다.

```  C
static void transposeDcm3x3(const double dSrcDcm[3][3],
                            double dDstDcm[3][3])
{
    dDstDcm[0][0] = dSrcDcm[0][0];
    dDstDcm[0][1] = dSrcDcm[1][0];
    dDstDcm[0][2] = dSrcDcm[2][0];

    dDstDcm[1][0] = dSrcDcm[0][1];
    dDstDcm[1][1] = dSrcDcm[1][1];
    dDstDcm[1][2] = dSrcDcm[2][1];

    dDstDcm[2][0] = dSrcDcm[0][2];
    dDstDcm[2][1] = dSrcDcm[1][2];
    dDstDcm[2][2] = dSrcDcm[2][2];
}
```



## 5. Step 4 — `V_B_des = R_EB * V_E_ref` (현재 Base에서의 목표 LOS)

`V_E_ref`는 ENU 기준의 “타겟 방향”이다.
 이를 현재 자세의 Base에서 바라보면 어떻게 보이는지 계산한다.
$$
V_{B,des} = R^{E}_{B} \cdot V_{E,ref}
$$

### 5.1 행렬 × 벡터

```c
static VECTOR3 rotateVector(const double R[3][3], const VECTOR3* pstVector3)
{
    VECTOR3 stOutVector3;

    stOutVector3.dX = R[0][0]*pstVector3->dX + R[0][1]*pstVector3->dY + R[0][2]*pstVector3->dZ;
    stOutVector3.dY = R[1][0]*pstVector3->dX + R[1][1]*pstVector3->dY + R[1][2]*pstVector3->dZ;
    stOutVector3.dZ = R[2][0]*pstVector3->dX + R[2][1]*pstVector3->dY + R[2][2]*pstVector3->dZ;

    return stOutVector3;
}
```

### 5.2 계산

```c
double R_BE[3][3];
double R_EB[3][3];

VECTOR3 V_E_ref;   // 기준 시점 저장값 (단위벡터)
VECTOR3 V_B_des;   // 현재 IMU 기준에서의 목표 LOS (단위벡터)

// 1) 새 IMU Euler → R_BE
calcDcmFromEuler(rollRad, pitchRad, yawRad, R_BE);

// 2) R_EB = transpose(R_BE)
transposeDcm3x3(R_BE, R_EB);

// 3) V_B_des = R_EB * V_E_ref
V_B_des = rotateVector(R_EB, &V_E_ref);
```



## 6. Step 5 — `V_B_des` → (AZ_cmd, EL_cmd) 변환

Base 좌표계에서의 시선벡터가
$$
V_B = [x, y, z]^T
$$
일 때, (당신의 AZ/EL 정의와 일치)

- AZ는 수평면에서 +X 기준 회전각
- EL은 수평면에서 위쪽(+Z)으로의 각도

권장식:
$$
AZ = atan2(y, x)
$$

### 6.1 코드 (Vector → AZ/EL)

```c
static void vectorToAzEl(const VECTOR3* pstVector3, double* pdAzRad, double* pdElRad)
{
    double dX = pstVector3->dX;
    double dY = pstVector3->dY;
    double dZ = pstVector3->dZ;

    *pdAzRad = atan2(dY, dX);
    *pdElRad = atan2(dZ, sqrt(dX*dX + dY*dY));
}
```



### 6.2 최종 명령각 산출

```c
double azCmdRad, elCmdRad;

vectorToAzEl(&V_B_des, &azCmdRad, &elCmdRad);

// azCmdRad, elCmdRad → 페데스탈 제어에 사용
```

------



# 7. 상대회전(Relative Rotation)의 도입

## 7.1 왜 상대회전이 필요한가?

기존 절차에서는 다음을 수행합니다:
$$
V_{B,des} = R_{EB,curr} \; V_{E,ref}
$$
이는 수학적으로 정확하지만, 내부적으로는 다음 연산을 수행하는 것과 같습니다:
$$
V_{B,des}
=
R_{EB,curr}
\;
R_{BE,ref}
\;
V_{B,ref}
$$
즉,
$$
V_{B,des}
=
(R_{EB,curr} \; R_{BE,ref})
\;
V_{B,ref}
$$


------

## 7.2 상대회전의 의미

$$
R_\Delta = R_{curr}^T R_{ref}
$$

> 기준 자세 대비 현재 자세의 차이 회전을 의미한다.



## 8. Step 6 — 최종 “새 IMU 수신 시 처리” 함수 예시

```c
typedef struct {
    double dX;
    double dY;
    double dZ;
} VECTOR3;

typedef struct
{
    double R_ref[3][3];   // 기준 IMU 자세 (Body→ENU)
    VECTOR3 V_B_ref;      // 기준 Body LOS (AZ0, EL0)
} STABILIZER_REF;


void stabilizerUpdate(const STABILIZER_REF* pstRef,
                      double dRollRad,
                      double dPitchRad,
                      double dYawRad,
                      double* pdAzCmdRad,
                      double* pdElCmdRad)
{
    double R_curr[3][3];
    double R_curr_T[3][3];
    double R_delta[3][3];

    VECTOR3 V_B_cmd;

    /* 1) 현재 IMU DCM (Body→ENU) */
    calcDcmFromEuler(dRollRad, dPitchRad, dYawRad, R_curr);

    /* 2) R_curr^T (ENU→Body) */
    transposeDcm3x3(R_curr, R_curr_T);

    /* 3) R_delta = R_curr^T * R_ref */
    matMul3x3(R_curr_T, pstRef->R_ref, R_delta);

    /* 4) 기준 Body LOS에 상대회전 적용 */
    V_B_cmd = rotateVector(R_delta, &pstRef->V_B_ref);

    /* 5) AZ / EL 계산 */
    vectorToAzEl(&V_B_cmd, pdAzCmdRad, pdElCmdRad);
}
```