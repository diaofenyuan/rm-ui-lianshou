param(
    [string]$Python = '',
    [switch]$Force,
    [switch]$WithBuildTools,
    [switch]$UseDefaultIndex
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot

$venvDir = '.venv'
$venvPython = Join-Path $venvDir 'Scripts/python.exe'
$indexArgs = if ($UseDefaultIndex) { @() } else { @('-i', 'https://pypi.tuna.tsinghua.edu.cn/simple') }
$mirrorHost = if ($UseDefaultIndex) { 'pypi.org（默认源）' } else { 'pypi.tuna.tsinghua.edu.cn（清华源）' }

function Info([string]$message) { Write-Host "[setup] $message" }
function Fail([string]$message) { Write-Host "[setup] 失败：$message" -ForegroundColor Red; exit 1 }

# 1. 定位解释器：优先 -Python 参数，其次 py -3.11，最后 python
if (!$Python) {
    $candidates = @()
    try {
        $py311 = (& py -3.11 -c "import sys; print(sys.executable)" 2>$null)
        if ($LASTEXITCODE -eq 0 -and $py311) { $candidates += $py311.Trim() }
    } catch {}
    foreach ($name in @('python', 'python3')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { $candidates += $cmd.Source }
    }
    $Python = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
}
if (!$Python -or !(Test-Path $Python)) {
    Fail '未找到 Python 3.11。请安装 Python 3.11 后重试，或显式指定：-Python "C:\path\to\python.exe"'
}
$version = (& $Python -c "import sys; print('%d.%d' % sys.version_info[:2])").Trim()
if ($version -ne '3.11') {
    Info "警告：当前解释器为 Python $version，项目锁定的是 3.11（见 README 构建章节）。"
}
Info "使用解释器：$Python (Python $version)"

# 2. 创建虚拟环境
if (Test-Path $venvPython) {
    if ($Force) {
        Info '检测到已有 .venv，按 -Force 删除后重建。'
        Remove-Item -Recurse -Force $venvDir
    } else {
        Info '检测到已有 .venv，复用（如需重建请加 -Force）。'
    }
}
if (!(Test-Path $venvPython)) {
    Info '创建虚拟环境 .venv ...'
    & $Python -m venv $venvDir
    if ($LASTEXITCODE -ne 0) { Fail '创建虚拟环境失败。' }
}

# 3. 安装依赖
Info "升级 pip（源：$mirrorHost）..."
& $venvPython -m pip install --upgrade pip @indexArgs
if ($LASTEXITCODE -ne 0) { Fail 'pip 升级失败，请检查网络。' }

$installArgs = @('-m', 'pip', 'install') + $indexArgs + @('-r', 'tools/requirements.txt')
if ($WithBuildTools) {
    $installArgs += @('aqtinstall==3.3.0', 'cmake', 'ninja')
    Info '额外安装构建工具（aqtinstall / cmake / ninja）。'
}
Info '安装模拟端依赖（tools/requirements.txt）...'
& $venvPython @installArgs
if ($LASTEXITCODE -ne 0) { Fail '依赖安装失败。' }

# 4. 校验
Info '校验依赖可导入...'
$check = & $venvPython -c "import amqtt, paho.mqtt, google.protobuf as p; print('ok', p.__version__)"
if ($LASTEXITCODE -ne 0) { Fail '依赖导入校验未通过。' }
Info "依赖就绪：protobuf $($check -replace '^ok\s+', '')"

# 5. 生成 protobuf Python 代码（模拟端导入游戏状态消息时需要）
$genDir = 'build/generated'
$protoc = Get-ChildItem -Path 'build/_deps' -Filter 'protoc.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($protoc -and (Test-Path 'build/generated/game_status_pb2.py') -and -not $Force) {
    Info 'protobuf Python 代码已存在，跳过生成。'
} elseif ($protoc) {
    New-Item -ItemType Directory -Force $genDir | Out-Null
    & $protoc.FullName --python_out=$genDir --proto_path=proto proto/game_status.proto proto/rm_messages.proto
    if ($LASTEXITCODE -eq 0) { Info "已生成 protobuf Python 代码：$genDir" }
    else { Info '生成 protobuf Python 代码失败（需先成功构建一次 C++ 工程以产出 protoc.exe）。' }
} else {
    Info '未找到 protoc.exe，跳过 protobuf 代码生成；先运行 tools\build.ps1 -Setup -Package 完成构建后会产出。'
}

Write-Host ''
Info '完成。后续用法：'
Write-Host '  启动演示：  .\start-demo.cmd'
Write-Host '  校验 PLUS1：.\.venv\Scripts\python.exe -X utf8 tools\check_plus1.py'
Write-Host '  校验链路：  .\.venv\Scripts\python.exe -X utf8 tools\check_link.py'
