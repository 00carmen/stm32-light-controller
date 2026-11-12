# GitHub 发布说明

建议仓库名：`stm32-light-controller`。

建议描述：STM32F103C8 光照采集与三模式渐变调光，配套 Python 串口控制台、实时曲线、CSV 记录与 Flash 参数保存。

建议Topics：`stm32`、`stm32f103`、`embedded`、`pwm`、`uart`、`tkinter`、`learning-project`。

## 本次准备了什么

发布准备目录包含源码、可打开的Keil工程、Python上位机、软件测试、学习手册、完整接线、协议、存储说明、验收依据和实际示例CSV。ZIP发布包不包含运行环境、编译产物和个人调试文件。目录内验证生成的构建缓存由.gitignore排除，git add不会提交这些文件。本次公开仓库配置见下文，具体提交与推送结果以Git历史及远程main分支为准。

准备版会移除固件中未使用的旧LED、Key和CountSensor模块及工程引用，原来正在使用的p7工程不作这类修改。复制后的固件需要安装MDK、AC6和对应器件包；它不是包含商业编译器的免安装工程。

本轮新增CHANGELOG.md、docs/Release说明草稿.md、docs/上传前剩余事项.md和GitHub Actions测试配置。首次发布无需等待照片或视频齐全；账号、可见性和基础教程来源现已确认。Word手册和源码无需重新制作。

## 当前账号与来源确认

本次使用00carmen账号，公开仓库地址为https://github.com/00carmen/stm32-light-controller 。本机登录已核实，提交使用GitHub noreply邮箱。以下占位符命令供其他电脑或新仓库复刻，不需在已经完成首次提交的目录中重复初始化。

基础参考已确认：[江协科技STM32入门教程](https://www.bilibili.com/video/BV1th411z7sn)。作者明确LED、ADC、基础传输等参考或使用教程代码。OLED驱动、字库与部分模板的逐文件出处仍待追溯，不据课程链接推定统一许可；原有ST和ARM声明保留。

剩余展示材料为实物照片、视频等；正式Release和统一授权可在来源、复刻及测试核对后再处理。实际首次推送与自动测试状态，以远程main和Actions为准。

## 发布前检查

1. 读README，确认功能与自己的理解一致。实际功能是环境明暗映射调光，不写成指定照度闭环、PID控制、RTOS或ROS 2项目。
2. 确定公开还是私有。先私有整理也可以，面向求职展示时再决定是否公开。公开前补充教程来源、OLED驱动和字库出处；原有ST、ARM声明须保留。
3. 确认源代码和文档中没有账号口令、访问令牌、个人简历、调试历史、私人路径截图等不想公开的内容。示例CSV包含实际采集时间，无账号信息。
4. 检查THIRD_PARTY_NOTICES.md。当前没有擅自添加统一MIT许可证，后续应针对自己有权授权的部分选择许可证，第三方部分继续按其各自条款处理。

## 建立第一次本地提交

准备版使用main分支；本次上传将配置上述origin和提交身份。打开该文件夹中的终端，先确认位置，再执行：

如果使用本次ZIP压缩包解压得到的副本，它不包含.git历史目录，需先执行`git init -b main`，再执行下面的提交命令。

```powershell
git status
git config user.name "你的GitHub用户名"
git config user.email "你的GitHub noreply邮箱"
git add .
git diff --cached --stat
git diff --cached
git commit -m "Add STM32 light controller baseline"
```

两个config只针对当前仓库，不加--global。身份占位符必须替换成自己的值，不要原样粘贴。学习手册是二进制DOCX，git diff不能直接展示正文，提交前先在Word中审阅。

## 创建GitHub仓库并上传

1. 在自己的GitHub账号下新建仓库，名称用stm32-light-controller或自己选定的名称，选择可见性。
2. 建立空仓库，不再让GitHub自动生成README、.gitignore或LICENSE，以免与已有目录产生不同的初始历史。
3. 使用已安装Git的凭据管理器完成HTTPS认证，或使用已配置的SSH。当前电脑未检测到gh命令，不依赖GitHub CLI。
4. 复制仓库的实际HTTPS地址，替换下面地址中的YOUR_NAME后执行：

```powershell
git remote add origin https://github.com/00carmen/stm32-light-controller.git
git remote -v
git push -u origin main
```

若已经有origin，先核对git remote -v，确认目标后再设置地址。不要把访问令牌拼进URL，不要上传凭据文件。如果登录失败，先解决账号认证，不在聊天或README里粘贴令牌。

## 上传后核对

打开GitHub首页，检查目录层级、中文显示、接线表、协议链接、示例CSV和手册下载。确认没有.venv、Objects、Listings、uvguix、运行日志。建议另找一个新文件夹克隆后，按README复刻一次，而不是只用原来已有环境验证。

进入Actions查看Host software tests，核对C与Python两项作业。当前只验证本地测试和工作流配置，尚未实际运行GitHub云端作业。Python作业使用虚拟显示运行Tk测试；工作流不烧录开发板，不使用Keil商业编译器构建目标固件。

整理完成并核对后，可以创建`v1.0.0`标签，表示第一个完成本轮验收的完整项目；固件内部仍是V1.8，上位机仍是V0.2，版本名称含义要在Release里写清。正式发布时再创建标签，不提前宣称已有远程Release。

## 后续更新方式

每次只解决一个明确问题，先记录现象和预期，再改代码、验证、提交。新增功能更新README和接线表；改变协议同步更新协议说明和测试；新增依赖更新requirements.txt。不要只上传一个新的“最终版压缩包”覆盖全部历史。

## 官方参考

- [本地代码上传GitHub](https://docs.github.com/en/migrations/importing-source-code/using-the-command-line-to-import-source-code/adding-locally-hosted-code-to-github)
- [忽略不应提交的文件](https://docs.github.com/en/get-started/git-basics/ignoring-files)
- [仓库许可证说明](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository)
- [checkout官方Action](https://github.com/actions/checkout)
- [setup-python官方Action](https://github.com/actions/setup-python)
