# 首次Release说明草稿

以下内容用于完成首次上传并核对之后，在GitHub Release页面填写。现在不要将其当成已经发布的Release。

建议标签：`v1.0.0`

建议标题：`v1.0.0 STM32光照监测与三模式调光`

## 功能

首个完成本轮功能验收的项目组合：STM32固件V1.8与Python上位机V0.2。

提供光敏和旋钮采集、滤波与明暗校准、自动/旋钮/远程三种模式、PWM渐变、OLED与按键交互、串口命令、Flash参数保存、电脑实时曲线及CSV记录。

## 使用入口

- 固件：firmware/p7.uvprojx，Keil MDK与AC6，目标STM32F103C8。
- 上位机：Windows下运行software/pc_app/start.cmd，需要含Tk的Python与可用USB串口。
- 接线：docs/硬件接线与复刻.md。
- 学习与协议：docs中的Word手册、在线版、PROTOCOL.md与STORAGE.md。
- 实际数据：examples中的两份CSV。

## 验证

固件Keil编译0错误、0警告；C主机测试56项协议、20项配置、233项存储检查，Python测试27项。实物操作已验证三模式、保存恢复、曲线、CSV、短时持续运行及重连。

上传后如GitHub Actions首次运行通过，可另写明通过的工作流和提交号；未通过前不添加绿色通过徽章或宣称云端验证完成。

## 注意

相对光照不是物理lux；本项目按明暗映射调光，没有恒照度PID。REMOTE断开电脑后保留目标，可按PB1回AUTO。重新烧录可能擦除Flash参数；下载前保留0xF800程序区限制。

公开发布前补充原教程、OLED与字库来源，并按实际权利确定授权范围。不要把第三方库统一改标为原创或MIT。
