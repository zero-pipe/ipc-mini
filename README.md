# ipc-mini是什么

基于Hi3516CV610 SoC 芯片SDK开发的WebRTC 套件，已实现WebRTC直播，带sd存储卡/硬盘时可通过配置开启fmp4格式文件录像，已实现文件上传云端、云端hls拉流播放。

## Clone代码

```bash
git clone --recursive https://github.com/zero-pipe/ipc-mini.git
cd ipc-mini
git submodule update --init --recursive
```

## ipc-mini的意义

1、学习和熟悉海思编解码SoC芯片的SDK及封装。

2、音视频时间同步处理机制，H264不编码B帧，DTS=PTS。

3、如何通过MediaSource抽象媒体源、通过DeviceAdapter适配不同的芯片SDK。

4、WebRTC中信令服务/ICE/STUN/TURN协议如何配合使用。

5、看多少理论都不如自己实实在在调试一遍理解透彻、记忆牢固。

