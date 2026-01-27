# 🐳 WhaleBot 智能鲸鱼小车 (ESP32-S3 AI Robot)

**WhaleBot** 是一个基于 **ESP32-S3** 的全向移动 AI 机器人项目。它集成了 **Coze (扣子) 大模型**、**百度语音技术**、**麦克纳姆轮**全向运动控制以及**串口触摸屏**，实现了“听、说、想、动、看”一体化的智能交互体验。

---

### 🤝 Datawhale x 星芯工坊 | AI 硬件跟班学习

## 📚 开发教程 (Documentation)

本项目联合国内最大的 AI 开源社区 **Datawhale** 发起【AI 硬件入门跟班学习】活动！

Datawhale 致力于构建“For the learner, 构建 AI 学习者的开源社区”，汇聚了众多 AI 领域的专家与爱好者。

本次课程将带领大家从零开始，手搓一台属于自己的 AI 机器人。

我们编写了保姆级的开发教程，涵盖硬件选型、PCB 设计原理、代码注释及组装教程，并且会在后续配上视频教程。

👉 DataWhale官网教程链接**https://www.datawhale.cn/learn/summary/268** 👈

👉DataWhale跟班学习群👈

  <img width="400" height="400" alt="a497525bfc7f890d2fe31357c8119220" src="https://github.com/user-attachments/assets/a3c197dc-f527-4aca-8c82-df68392f9ef7" />

如果这一期跟班学习人数已满怎么办？没关系，你可以加入我们的项目交流微信群，同样可以得到技术和硬件支持！

  <img width="396" height="396" alt="image" src="https://github.com/user-attachments/assets/99fd9e6a-3d8f-402e-8d46-a8bf989573f7" />

### 🛍️ 新手无忧套件

为了让初学者免去元器件采购发货慢、型号买错的烦恼，我们提供了**一站式全套件**（包含 3D 打印外壳、PCB 成品/半成品、电机、电池及所有电子料）。这些物料消息会在跟班学习官方群和我们的项目讨论群中同步更新，可以在群内咨询~

---

## 🛠️ 硬件架构

### 硬件清单
*   **主控**：ESP32-S3-DevKitC (N16R8)
*   **驱动**：TB6612FNG 2片
*   **执行器**：N20 减速电机 (12V 300RPM) 4个 + 麦克纳姆轮 4个（一套）
*   **音频**：INMP441 麦克风 + MAX98357A 功放 ＋ 
*   **电源**：12V 锂电池 + TPS5430 降压模块
*   **交互**：串口触摸屏 / 天问 ASRPRO 开发板
*   ps：电子器件部分如需自行购买请参考前文提到的“Datawhale官网开发教程”（后简称“教程”）第一大章“DIY所需硬件”的购买推荐
*   **外形**：详见docs/外壳打印文件
*   **PCB**：详见hardware/PCB及原理图

---

## 💻 复刻：快速上手指南

### 一. 硬件部分组装
1.准备好pcb板，按照板上引脚位置指示连接电子器件。
ps：若自行打板请参照hardware/Bom表购买相关电子元件并自行焊接。
2.外壳组装，外壳前后端与底板留有固定贴，方便拆卸调试，外壳文件详见docs/外壳打印。

### 二. esp32-s3程序部分
1.下载Arduino IDE（推荐版本2.1.4及以上）。
2.下载esp32芯片包，离线下载包位于firmware/esp32芯片包，如果不会离线下载请参考“教程”第三大章“软件部分”。
3.安装支持库如下
<base64.hpp> （建议版本1.3.0） 
<ArduinoJson.h> （建议版本7.4.1）
<UrlEncode.h> （建议版本1.0.1）
4.在烧录代码前按照下图所示进行相应选项的勾选！！
<img width="1035" height="1013" alt="image" src="https://github.com/user-attachments/assets/6d8616cf-2cc3-4750-9be2-da74c710cbc2" />
代码文件位于firmware/WhaleBot_Main.ino。

---

## 🤝 致谢

本项目由MeetAI实验室发起并完成项目开发，历时9个月最终成功制作出WhaleBot AI鲸鱼小车，并制作了一套完整的教程。在此衷心感谢Datawhale社区，全体参与开发的小伙伴，提供帮助的指导老师和同仁们。MeetAI实验室是来自福州大学的计算机学院的实验室，创立于2024年，致力于打造一个有趣好玩多元的104。
实验室的开发者们
<img width="397" height="397" alt="image" src="https://github.com/user-attachments/assets/97f338a7-257a-4607-9d25-f6e582761c53" />
开发者名单
项目开发：林镐轩，尚启峰，解皓喆
二次开发：Neil·Bauman，猫猫草，gtxylt，kyle
教程制作：林镐轩，kyle，小班，青莳与空，Neil·Bauman，猫猫草，李诗语，刘宣佑


如果你觉得这个项目有趣，请点击右上角的 ⭐️ **Star** 支持我们！

## 📄 License

[MIT License](LICENSE) © 2024 StarChip Workshop (星芯工坊)
```
