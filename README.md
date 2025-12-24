# PixelPortal: ESP32 Minecraft MiniMap & Smart Lamp Controller 🛰️💡

[![Platform](https://img.shields.io/badge/Platform-ESP32-blue)](https://www.espressif.com/en/products/socs/esp32)
[![Python](https://img.shields.io/badge/Python-3.9+-yellow)](https://www.python.org/)
[![Java](https://img.shields.io/badge/Java-17+-orange)](https://www.oracle.com/java/)
[![Arduino](https://img.shields.io/badge/Arduino-C++-red)](https://www.arduino.cc/)
[![Minecraft](https://img.shields.io/badge/Minecraft-1.21.x-green)](https://www.minecraft.net/)

> **打破次元壁：** PixelPortal 是一个将 **Minecraft 实时同步小地图**与**现实智能家居控制**完美结合的混合现实桌面终端。基于 ESP32 与 64x64 HUB75 LED 矩阵屏构建。

---

## 💖 致谢与声明
本项目在视觉设计与数据驱动上深受开源社区的启发，特别鸣谢：
* **[Clockwise](https://github.com/jnthas/clockwise)**：本项目马里奥风格的时钟界面在视觉设计上参考并致谢了 Clockwise 及其相关马里奥时钟开源项目。
* [cite_start]**[Squaremap](https://github.com/jpenilla/squaremap)**：感谢 Squaremap 开发团队提供的强大地图渲染模组。本项目通过 Python 中间件高效抓取其生成的瓦片数据实现地图展示 [cite: 15, 28]。

---

## 📁 项目结构 (Project Structure)

```text
PixelPortal/
├── src/                  # ESP32 源代码
│   └── main.cpp          # 核心固件：UDP小地图接收 + 智能灯控逻辑 + 马里奥 UI
├── python_server/        # Python 中间件 (后端服务)
[cite_start]│   ├── app.py            # 核心适配器：Squaremap 瓦片处理与 UDP 数据推送 [cite: 15]
│   ├── deploy.sh         # 部署脚本
│   └── Dockerfile        # 容器化部署支持
├── Mod version/          # [开发中] 自研本地 Mod 源码 (目前存在已知 BUG)
├── Mincreft_server/      # 插件配置参考
[cite_start]│   ├── comfig.yml        # Squaremap 渲染器配置文件 [cite: 1]
│   └── squaremap-paper-mc1.21.10...jar # 推荐模组版本
├── PCB/                  # 硬件电路工程文件
│   └── HUB75.eprj        # 矩阵屏连接工程 (立创 EDA)
├── docs/                 # 项目媒体资源 (包含运行效果图)
└── platformio.ini        # 硬件库依赖管理 (ESP32-HUB75-MatrixPanel-I2S-DMA 等)
```

---

## 🛠️ 硬件清单 (Bill of Materials)

| 组件名称 | 推荐规格/型号 | 数量 | 功能说明 |
| :--- | :--- | :--- | :--- |
| **LED 矩阵屏** | [64x64 HUB75 接口 P3 屏幕](https://mobile.yangkeduo.com/goods.html?ps=SEjg0cVlbL) | 1 | 显示小地图、UI 元素及马里奥动画 |
| **人体感应器** | [数字人体红外传感器](https://mobile.yangkeduo.com/goods1.html?ps=aNu6gXaFw6) | 1 | 检测移动状态，触发屏幕唤醒及灯控逻辑 |
| **主控板** | ESP32-WROOM-32 | 1 | 核心控制单元，负责 WiFi/蓝牙通信与屏驱 |
| **光敏传感器** | 5516 LDR 光敏电阻 | 1 | 闭环检测环境光亮度，辅助判断物理灯状态 |
| **杜邦线/排线** | 16P 数据排线 + 优质杜邦线 | 若干 | 硬件链路物理连接 |

---

## ✨ 核心特性

### 🗺️ Minecraft 小地图 (MiniMap Mode)
* [cite_start]**实时渲染推送**：Python 中间件自动计算玩家坐标瓦片，并使用 UDP 分片协议高效推送至屏幕 [cite: 15, 26, 32]。
* [cite_start]**战术 UI**：屏幕实时同步玩家坐标红点、朝向以及实时生命值效果 [cite: 25, 27, 32]。
* [cite_start]**多级缩放**：支持 16格 到 128格 的实时缩放分辨率调整 [cite: 16, 24, 27]。

### 💡 智能灯控 (Smart Lamp)
* **逻辑互斥**：通过 LDR 光敏电阻识别物理手动关灯，解决感应灯与手动操作的逻辑冲突。
* **冷却屏蔽**：检测到物理关灯后自动进入 5 秒屏蔽期，防止因人体移动再次误触发。
* **指令重试**：针对蓝牙灯具内置最高 300 次“补刀”重试逻辑，确保指令成功执行。

### 🎮 复古美学 (Mario Clock)
* **自动切换**：地图信号超时 5 秒后自动切回马里奥时钟模式。
* **感应唤醒**：通过 PIR 传感器实现人来屏亮，并动态显示感应灯熄灭倒计时。

---

## 🚀 快速部署与关键设置

### 1. 服务器端 (Minecraft Server) 重要设置
* **内存管理**：**务必**将服务器的 `Xmx` 限制在 **(总物理内存 - 1.5G)** 以下。Squaremap 渲染瓦片会占用大量非堆内存。
* **地图初始化**：安装插件后，必须运行以下命令生成初始地图：
  ```bash
  /squaremap fullrender <world_name>
  ```

### 2. 固件与功能配置
* **灯控开关**：若不需要物理灯控，请在代码中注释掉 `sendSeparatePacket()` 调用。**马里奥 UI 逻辑会完整保留**。
* [cite_start]**视图限制**：目前受限于 Squaremap 渲染稳定性，系统强制使用 `surface` (地表) 视图展示 [cite: 4, 28]。

---

## ⚠️ 开发者声明 (Current Status)
1. [cite_start]**Cave 模式**：虽有接口预留，但目前存在 Squaremap 渲染稳定性问题，**Cave 模式暂未解决** [cite: 15, 23]。
2. **Mod Version**：包含了我自研的本地 Mod 源码。目前**存在已知 BUG**，仅供学习参考。
3. **AI 辅助开发声明**：本项目的代码逻辑优化、文档撰写以及部分 Bug 修复方案是在 Gemini AI 的辅助下完成的。AI 的参与极大地提升了开发效率，在此予以声明。

---

## 📺 视频演示与社区
访问 B 站主页查看 PixelPortal 的实物运行视频：
**[Creamy-coffee 的 B 站主页](https://space.bilibili.com/520910365)**

**如果你喜欢这个项目，欢迎在 B 站给我的视频一个三连支持！🌟**