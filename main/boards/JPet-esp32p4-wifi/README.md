# 编译使用说明
* idf version: v5.5

1. 设置编译目标为 esp32p4

```shell
idf.py set-target esp32p4 
```

2. 修改配置 

```shell
cp main/boards/JPet-esp32p4-wifi/sdkconfig.JPet-esp32p4-wifi sdkconfig
```

3. 编译烧录程序

```shell
idf.py build flash monitor
```

> [!NOTE]
> 进入配网模式：按下复位按键后2s内按一次BOOT按键，直到听到配网模式

 
## TODO
