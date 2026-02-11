# ENU 월드 좌표계에서 페데스탈 장착 카메라 시선벡터 계산

## 1. 좌표계 정의

본 문서에서는 다음 좌표계를 사용한다.

### 1.1 ENU (Earth / World 좌표계)
- X_E : East
- Y_E : North
- Z_E : Up
- 오른손 좌표계

### 1.2 Body 좌표계 (페데스탈 하부, IMU 기준)
- X_B : 전방
- Y_B : 우측
- Z_B : 위쪽
- 오른손 좌표계

### 1.3 Top 좌표계 (카메라 장착 상부)
- 카메라는 상부 좌표계의 +X 방향을 전방 시선으로 가짐

---

## 2. 카메라 기본 시선벡터 (Top 좌표계)

카메라는 상부 좌표계에서 항상 +X 방향을 바라본다고 가정한다.

```math
V_T = [1,\;0,\;0]^T
```

## 3. 페데스탈 방위각 / 고각 정의

- 방위각 (AZ):
  - Z축(Up)을 기준으로 X → Y 방향(오른손) 회전
  - AZ = 0° → 전방(X_B)
- 고각 (EL):
  - 수평면(X-Y)에서 위쪽(Z_B) 방향으로 증가



## 4. Body 좌표계에서의 시선벡터 계산

페데스탈의 방위각(AZ), 고각(EL)을 이용하여
 카메라 시선벡터를 Body 좌표계에서 표현하면 다음과 같다.
$$
V_B =
\begin{bmatrix}
\cos(EL)\cos(AZ) \\
\cos(EL)\sin(AZ) \\
\sin(EL)
\end{bmatrix}
$$

### 4.1 C 코드 (AZ/EL → V_B)

```c
typedef struct {
    double dX;
    double dY;
    double dZ;
} VECTOR3;


static VECTOR3 calcLosVectorToBody(double dAzRad, double dElRad)
{
    VECTOR3 stVector3;

    stVector3.dX = cos(dElRad) * cos(dAzRad);
    stVector3.dY = cos(dElRad) * sin(dAzRad);
    stVector3.dZ = sin(dElRad);

    return stVector3;
}
```



## 5. IMU Euler Angle 정의 (Roll / Pitch / Yaw)

IMU(MTi-670)에서 출력되는 Euler 각은 다음 순서를 따른다.

- Roll  (φ): X축 회전
- Pitch (θ): Y축 회전
- Yaw   (ψ): Z축 회전

회전 순서 (Body 기준, Intrinsic):
$$
R = R_z(Yaw) \cdot R_y(Pitch) \cdot R_x(Roll)
$$


## 6. Euler → DCM(Direction Cosine Matrix)

### 6.1 각 축 회전 행렬

### Roll (X축 회전)

$$
R_x(\phi) =
\begin{bmatrix}
1 & 0 & 0 \\
0 & \cos\phi & -\sin\phi \\
0 & \sin\phi & \cos\phi
\end{bmatrix}
$$

------

### Pitch (Y축 회전)

$$
R_y(\theta) =
\begin{bmatrix}
\cos\theta & 0 & \sin\theta \\
0 & 1 & 0 \\
-\sin\theta & 0 & \cos\theta
\end{bmatrix}
$$

------

### Yaw (Z축 회전)

$$
R_z(\psi) =
\begin{bmatrix}
\cos\psi & -\sin\psi & 0 \\
\sin\psi & \cos\psi & 0 \\
0 & 0 & 1
\end{bmatrix}
$$

------

### 회전 순서 요약 (IMU / Body 기준, Intrinsic)

$$
R = R_z(\psi)\,R_y(\theta)\,R_x(\phi)
$$



### 6.2 C 코드 (Euler → DCM)

```c
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

본 DCM은 **Body → ENU (B → E)** 변환 행렬이다.



## 7. Body → ENU 좌표계 시선벡터 변환

Body 좌표계에서 계산된 시선벡터 `V_B`를
 ENU 좌표계 시선벡터 `V_E`로 변환한다.
$$
V_E = R^{B}_{E} \cdot V_B
$$

------

### 7.1 C 코드 (DCM × Vector)

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



## 8. 최종 시선벡터 계산 흐름 요약

```
카메라 기본 시선
    ↓
페데스탈 AZ / EL 적용
    ↓
Body 좌표계 시선벡터 V_B
    ↓
IMU Euler → DCM (R_EB)
    ↓
ENU 좌표계 시선벡터 V_E
```



## 9. 전체 예제 코드 흐름

```
Vec3 vBody, vEarth;
double R_EB[3][3];

// 1. 페데스탈 AZ/EL → Body LOS
vBody = calcLosVectorBody(azRad, elRad);

// 2. IMU Euler → DCM
calcDcmFromEuler(rollRad, pitchRad, yawRad, R_EB);

// 3. Body → ENU 변환
vEarth = rotateVector(R_EB, &vBody);
```

