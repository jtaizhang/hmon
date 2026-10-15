# hmon

A native terminal system monitor for **HarmonyOS PC / HiShell**, inspired by
[macmon](https://github.com/vladkens/macmon). C++17, MIT licensed, no sudo.

鸿蒙 PC 上的终端监控工具：CPU 逐核负载、频率、内存、进程和可读温度的历史图。
面向 HarmonyOS 7.0 arm64；直接在 HiShell 运行，无需连接开发电脑。

## 功能

- CPU 总负载、逐逻辑核历史图、系统 cpufreq 分组和 MHz。
- RAM / Swap 已用与总量；GPU 内存独立标注。
- 当前身份可见进程的 CPU、RSS 和可选 PSS；搜索、排序、滚动。
- Hardware 页分别显示频率和温度，每个指标都有独立子图。
- 120 个样本的历史、均值和峰值；暂停产生缺测空隙。
- 自适应终端大小，支持纯图表模式、ANSI 主题和无颜色输出。
- JSON Lines 输出、自检、数据源诊断。

## 安装

发行包面向 HarmonyOS PC 的 HiShell，不能在 Windows、macOS 或 Linux 虚拟机中直接运行。
先按 [Harmony Brew 官方说明](https://harmonybrew.atomgit.com/) 配置包管理器。
可从维护者的 tap 安装：

```sh
brew tap jtaizhang/hmon https://github.com/jtaizhang/homebrew-hmon.git
brew install jtaizhang/hmon/hmon
hmon
```

这是 hmon 的独立 tap，不代表已进入 Harmony Brew 核心软件仓库。
若 Brew 要求信任新 tap，请确认来源为 `jtaizhang/homebrew-hmon` 后按提示操作。

也可以从 [Releases](https://github.com/jtaizhang/hmon/releases) 下载
`hmon-0.2.7-harmonyos-arm64.tar.gz`，在 HiShell 解压并启动：

```sh
tar -xzf hmon-0.2.7-harmonyos-arm64.tar.gz
cd hmon-0.2.7-harmonyos-arm64
chmod +x hmon hmon-core
./hmon
```

请始终启动 `hmon`。它通过 HiShell 的系统命令代理读取温度；直接启动
`hmon-core` 会跳过此通道。发行包已经自签名，请勿再 strip 原生可执行文件。

## 使用

| 按键 | 功能 |
| --- | --- |
| `v` | 总览 → 逐核心 → Hardware |
| `t` | 隐藏或显示进程，只看图表 |
| `s` | 循环选择 RSS / CPU / PSS 排序 |
| `/` | 按名称或 PID 搜索 |
| `←` / `→` | 有多页时翻页 |
| `↑` / `↓` | 滚动进程列表 |
| `r` | 切换 0.5 / 1 / 2 / 5 秒刷新 |
| `+` / `-` | 增加 / 减少 0.5 秒刷新间隔 |
| `p` / 空格 | 暂停或恢复 |
| `?` / `q` | 帮助 / 退出 |

```sh
hmon --compact
hmon --view hardware
hmon --pss --sort cpu
hmon --json --samples 10 > samples.jsonl
hmon --diagnose
hmon --self-test
```

详细说明见 [使用手册](docs/USAGE.txt)。

## 数据边界

| 指标 | 已验证行为 |
| --- | --- |
| CPU / RAM / Swap | HiShell 可读取；CPU 进程占用 100% 表示一个逻辑核 |
| CPU 频率 | 标明实测或请求频率；不把 cpufreq 分组猜测为大小核 |
| 进程内存 | 只覆盖当前身份可见进程；RSS 含共享页，不能直接相加 |
| 温度 | ThermalService 提供的机壳、电池、DDR 等；保留系统原名 |
| GPU 内存 | `GpuTotalUsed`，不是 GPU 使用率 |
| GPU 负载 / 频率 | 测试设备节点拒绝读取；不显示伪造的 0% |
| CPU/GPU 芯片温度 | 当前可读服务未导出，直接节点受限 |
| 风扇 / NPU / 功耗 | 尚无经验证的有效读数，不承诺支持 |

温度名称后 `*` 表示根据驱动名称推断区域，测点位置未确认。
CPU 供电区域温度不等于 CPU 芯片温度。未知数据为 `--` / JSON `null`。
权限和接口会随系统版本、设备及终端身份变化，安装包不会扩展系统权限。
可选的 `hmon-gpu-probe` 仅用于诊断，需要额外 SDK 头文件构建，默认监控不调用它。

## 构建与测试

Windows 上安装 DevEco Studio 后：

```powershell
pwsh -File scripts/Build-Hmon.ps1 -StudioPath 'C:\path\to\DevEcoStudio'
```

产物位于 `dist/`。构建只需要本机已安装的 SDK，不上传签名配置或账号凭据。
附加 `-WithGpuProbe` 可构建 GamePerformance 诊断工具。
`-Architecture x86_64` 生成模拟器测试包。模拟器没有 HiShell，应通过
HDC shell 执行 `./hmon-core --emulator`；它不使用真机的温度代理启动器。

在 POSIX C++17 环境下，可运行不访问真实硬件的测试：

```sh
make test
```

Harmony Brew 的源码构建应在其官方 `ci-runner` 容器中进行，见
[打包说明](docs/PACKAGING.md)。普通 Linux 上通过测试不等于通过鸿蒙真机验证。

0.2.7 已通过官方容器的源码安装、`brew test`、`brew style` 和
`brew audit --strict`，预编译 bottle 也通过重装测试。
鸿蒙 7.0 PC 已完成 Brew 安装、70 项自检与 2,568 种布局检查，
实际采集到 14 核 CPU 和 10 个温度测点。真机未安装编译工具时，
`brew test` 的 Ruby 测试依赖可能无法构建；可直接运行 `hmon --self-test`。

## 许可与致谢

[MIT](LICENSE)，Copyright 2026 Jiatai Zhang。
界面与交互参考 macmon；hmon 使用独立的鸿蒙采集实现，不调用 Apple 私有接口。
本项目不隶属于华为、Harmony Brew 或 macmon。
