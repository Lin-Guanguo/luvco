# luvco

Lua + libuv + coroutine

一个Lua的库，使用C语言编写了一个协程调度运行时，可以用Lua编写Go风格
的代码，表面同步，实则依靠运行时异步运行。摆脱回调，拥抱有栈协程。

## 进度

目前只完成了网络的一小部分，示例程序echo服务器见lua/test.lua。

## 使用方法

```bash
mkdir build && cd build
CMake ..
make
```
运行`./main-test`可以运行测试程序

运行`./driver`可以通过参数载入Lua文件。

## TODO

* Eventloop Graceful quit

* Chan1 send

* waiting data refactor

* closed lua state unregister
