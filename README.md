# zero-mini

Hi3516CV610 WebRTC 门铃固件。

## Clone

```bash
git clone --recursive https://github.com/zero-pipe/zero-mini.git
cd zero-mini
```

若已克隆但未拉子模块：

```bash
git submodule update --init --recursive
```

## 第三方依赖（`3rdpart/`）

| 路径 | 方式 |
|------|------|
| `zero-tool-kit` | git submodule |
| `mbedtls-2.28.8` | git submodule |
| `libsrtp` | git submodule（pinned commit） |
| `usrsctp` | git submodule（pinned commit） |
| `kvs-pic` | git submodule |
| `kvs-webrtc-sdk` | **源码入库**（相对上游有本地改动） |
| `jsoncpp` | **源码入库**（裁剪/集成用） |
| `freetype-2.7.1` | **源码/头文件+预编译库入库** |

交叉编译 KVS SDK 与固件在具备 musl 工具链的构建机上进行（`board/Makefile`，`ZERO_MINI_ENABLE_KVS=1`）。
