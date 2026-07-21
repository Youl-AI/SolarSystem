<#
.SYNOPSIS
    저장소에 담지 못한 대용량 텍스처를 GitHub Release에서 받아 온다.

.DESCRIPTION
    GitHub은 100 MiB가 넘는 파일을 거부한다. 아래 여섯 개가 거기 걸려서 Release
    첨부파일로 배포한다(첨부파일은 파일당 2 GB까지 되고 대역폭 제한이 없다).
    나머지 텍스처는 전부 저장소 안에 있으므로, 이 스크립트는 한 번만 실행하면 된다.

    이미 있고 크기가 맞으면 건너뛴다. 중간에 끊겼다면 다시 실행하면 된다.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\fetch_textures.ps1
#>

$ErrorActionPreference = "Stop"

$Tag  = "textures-v1"
$Base = "https://github.com/Youl-AI/Ephemeris/releases/download/$Tag"

# 받을 파일과 기대 크기(바이트). 크기를 적어 두면 중간에 끊긴 파일을 그냥 넘어가지 않는다.
$Files = @(
    @{ Path = "textures/planets/earth_diffuse.dds"; Size = 178957156 }
    @{ Path = "textures/planets/earth_clouds.dds";  Size = 178957156 }
    @{ Path = "textures/planets/earth_night.dds";   Size = 121548612 }
    @{ Path = "textures/planets/jupiter.dds";       Size = 138244020 }
    @{ Path = "textures/skybox_stars.dds";          Size = 134218036 }
    @{ Path = "textures/skybox_band.dds";           Size = 134218036 }
)

# 이 스크립트는 scripts\ 안에 있으므로 저장소 루트로 올라간다.
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$total = ($Files | Measure-Object -Property Size -Sum).Sum
Write-Host "대상 $($Files.Count)개, 합계 $([math]::Round($total/1MB)) MB"
Write-Host ""

$downloaded = 0
foreach ($f in $Files) {
    $dest = Join-Path $Root $f.Path
    $name = Split-Path -Leaf $f.Path

    if ((Test-Path $dest) -and ((Get-Item $dest).Length -eq $f.Size)) {
        Write-Host "  건너뜀  $name (이미 있음)"
        continue
    }

    $dir = Split-Path -Parent $dest
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

    Write-Host "  받는 중  $name ($([math]::Round($f.Size/1MB)) MB)"
    # 먼저 임시 파일로 받는다. 도중에 끊겨도 반쪽짜리가 제자리에 남지 않는다.
    $tmp = "$dest.part"
    try {
        # BITS가 이어받기와 진행률을 지원한다. 없으면 Invoke-WebRequest로 물러난다.
        if (Get-Command Start-BitsTransfer -ErrorAction SilentlyContinue) {
            Start-BitsTransfer -Source "$Base/$name" -Destination $tmp -Description $name
        } else {
            Invoke-WebRequest -Uri "$Base/$name" -OutFile $tmp -UseBasicParsing
        }
    } catch {
        Remove-Item $tmp -Force -ErrorAction SilentlyContinue
        Write-Host ""
        Write-Host "실패: $name" -ForegroundColor Red
        Write-Host "  $($_.Exception.Message)"
        Write-Host "  Release '$Tag' 에 파일이 올라가 있는지 확인하세요:"
        Write-Host "  https://github.com/Youl-AI/Ephemeris/releases/tag/$Tag"
        exit 1
    }

    $got = (Get-Item $tmp).Length
    if ($got -ne $f.Size) {
        Remove-Item $tmp -Force
        Write-Host ""
        Write-Host "크기가 다릅니다: $name — 받은 $got, 기대 $($f.Size)" -ForegroundColor Red
        Write-Host "  전송이 끊겼거나 Release의 파일이 갱신된 것입니다. 다시 실행해 보세요."
        exit 1
    }
    Move-Item -Force $tmp $dest
    $downloaded++
}

Write-Host ""
if ($downloaded -eq 0) {
    Write-Host "전부 이미 있습니다. 받을 것이 없습니다." -ForegroundColor Green
} else {
    Write-Host "$downloaded 개를 받았습니다. 이제 실행할 수 있습니다." -ForegroundColor Green
}
