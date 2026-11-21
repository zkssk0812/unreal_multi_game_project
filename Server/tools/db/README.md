# 최고 기록 DB (SQL Server)

서버는 판이 끝날 때 플레이어별 점수(= 반납 금액)를 SQL Server에 저장하고, 지난 최고 기록보다 높으면 갱신합니다 (명세 F-08, psd.md A-21).
DB가 없어도 서버는 돌아갑니다. 그때는 기록을 저장하지 않고, 결과 화면의 "최고 기록 갱신"이 항상 꺼져 있습니다.

## 준비 (한 번만)

1. **SQL Server**(Express 이상)와 **SSMS 22**를 설치합니다.
2. SSMS에서 [create_records.sql](create_records.sql)을 열고 실행합니다. `VaultHeist` 데이터베이스와 `dbo.PlayerRecord` 테이블이 생깁니다.
3. **ODBC Driver 18 for SQL Server**를 설치합니다(SSMS를 설치하면 함께 깔려 있는 경우가 많습니다).
4. [GameServer/server_config.example.json](../../GameServer/server_config.example.json)을 복사해 실행 파일 옆(`Binaries/x64/Debug/server_config.json`)에 두고 접속 문자열을 맞춥니다.

```json
{
  "db": {
    "connection_string": "Driver={ODBC Driver 18 for SQL Server};Server=localhost\\SQLEXPRESS;Database=VaultHeist;Trusted_Connection=yes;TrustServerCertificate=yes;"
  }
}
```

- `Server`는 SSMS에서 접속할 때 쓰는 서버 이름입니다 (예: `localhost`, `localhost\SQLEXPRESS`). JSON 안에서는 `\`를 `\\`로 씁니다.
- Windows 로그인 대신 SQL 계정을 쓰려면 `Trusted_Connection=yes;`를 `Uid=아이디;Pwd=비밀번호;`로 바꿉니다.
- `server_config.json`에는 비밀번호가 들어갈 수 있으므로 커밋하지 않습니다(`.gitignore`).

## 확인

서버를 켰을 때 로그에 `[DB] SQL Server 연결됨`이 보이면 됩니다. 판이 끝난 뒤 SSMS에서:

```sql
USE VaultHeist;
SELECT TOP 10 * FROM dbo.PlayerRecord ORDER BY best_score DESC;
```
