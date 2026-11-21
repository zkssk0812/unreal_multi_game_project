-- 「털어라! 금고!」 최고 기록 DB (psd.md A-21, T-72)
-- SSMS 22에서 이 파일을 열고 실행(F5)한다. 여러 번 실행해도 안전하다.

IF DB_ID(N'VaultHeist') IS NULL
    CREATE DATABASE VaultHeist;
GO

USE VaultHeist;
GO

-- 닉네임별 최고 점수(= 한 판의 반납 금액). 점수가 지난 기록보다 높을 때만 서버가 갱신한다 (F-08)
IF OBJECT_ID(N'dbo.PlayerRecord', N'U') IS NULL
CREATE TABLE dbo.PlayerRecord (
    nickname     NVARCHAR(16) NOT NULL PRIMARY KEY,   -- 플레이어 구분 (닉네임)
    best_score   BIGINT       NOT NULL,               -- 최고 반납 금액 ($)
    character_id SMALLINT     NOT NULL,               -- 기록을 세운 캐릭터 (1~5)
    updated_at   DATETIME2    NOT NULL DEFAULT SYSUTCDATETIME()
);
GO

-- 순위표를 볼 때 (높은 점수부터)
IF NOT EXISTS (SELECT 1 FROM sys.indexes WHERE name = N'IX_PlayerRecord_best_score')
    CREATE INDEX IX_PlayerRecord_best_score ON dbo.PlayerRecord (best_score DESC);
GO

-- 확인용: SELECT TOP 10 * FROM dbo.PlayerRecord ORDER BY best_score DESC;
