首个 MIT 开源发行版，面向 HarmonyOS 7.0 PC 的原生 HiShell。

- CPU 总负载、逐核与频率历史，RAM / Swap 已用与总量。
- 进程 CPU / RSS / 可选 PSS，Hardware 频率与温度子图。
- 修复 Harmony Brew 软链接启动；保留 HiShell 温度采集通道。
- 真机通过 70 项自检、2,568 种布局检查，并通过软链接读取 14 核 CPU 和 10 个温度测点；模拟器自检及 JSON 检查通过；Linux 便携自检 CI 通过。

下载 arm64 包用于鸿蒙 PC；x86_64 包仅用于模拟器，需通过 HDC shell 执行 hmon-core --emulator。每个包附 SHA-256 校验文件。

GPU 内存不等于 GPU 负载。目前测试设备的 GPU 负载、风扇转速、NPU 及 CPU/GPU 芯片温度仍不可读，缺失值不会显示成 0。进程列表仅覆盖当前身份可见进程。

源码与安装说明见 README。Harmony Brew 自定义安装源：https://github.com/jtaizhang/homebrew-hmon 。该 tap 不代表已进入 Harmony Brew 核心仓库。
