# ipc-mini

Hi3516CV610 WebRTC 开发套件 且支持fmp4格式切片存储。

## Clone

```bash
git clone --recursive <this-repo>
cd ipc-mini
git submodule update --init --recursive
```

录像依赖 `3rdpart/zero-media-kit`（[zero-pipe/zero-media-kit](https://github.com/zero-pipe/zero-media-kit.git)），和 `zero-tool-kit` 一样用子模块，不要再往仓库里拷源码。漏拉时：

```bash
git submodule update --init --recursive 3rdpart/zero-media-kit
```

