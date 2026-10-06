# 诊断工具

`usb3_mux_control.py` 通过 CH375 EP1 命令读取 CC ADC、MUX 模式、SEL 和 EN_N，也可临时强制 A/B 通道。

依赖 Windows、匹配系统架构的 CH372/CH375 驱动及 WCH `CH375DLL64.DLL`。

```powershell
python usb3_mux_control.py --dll C:\path\to\CH375DLL64.DLL status
python usb3_mux_control.py --dll C:\path\to\CH375DLL64.DLL auto
python usb3_mux_control.py --dll C:\path\to\CH375DLL64.DLL a
python usb3_mux_control.py --dll C:\path\to\CH375DLL64.DLL b
python usb3_mux_control.py --dll C:\path\to\CH375DLL64.DLL disabled
```

也可以设置环境变量：

```powershell
$env:CH375_DLL='C:\path\to\CH375DLL64.DLL'
python usb3_mux_control.py status
```

强制切换 MUX 会中断当前 USB3 链路；不要在采集过程中执行。

